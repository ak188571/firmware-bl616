#!/usr/bin/env python3
"""Exercise actual JTAG state transitions and actual FPGA loader cleanup."""
from pathlib import Path
import argparse,os,subprocess
root=Path(__file__).resolve().parents[2];build=root/'build-ui16-host';build.mkdir(exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--baseline');p.add_argument('--case',choices=['tap','loader'],default='tap');a=p.parse_args()
source=subprocess.check_output(['git','-C',str(root),'show',a.baseline+':fpga/programmer.cpp'],text=True) if a.baseline else (root/'fpga/programmer.cpp').read_text(encoding='utf-8')
extra=[]
if os.name=='nt':extra=['-BC:/msys64/ucrt64/bin/'];os.environ['MSYS2_ARG_CONV_EXCL']='*'
def run(name,code):
    file=build/(name+'.cpp');file.write_text(code,encoding='utf-8',newline='\n')
    exe=build/(name+('.exe' if os.name=='nt' else ''))
    subprocess.run(['g++',*extra,'-std=c++17','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-but-set-variable',str(file),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
tap=source[source.index('typedef enum tapState_t'):source.index('} tapState_t;')+len('} tapState_t;')]
writer=source[source.index('void jtag_writeTDI_msb_first_gpio_out_mode'):source.index('\n/*\nstatic int jtag_writeTDI_slow')]
states=source[source.index('static void setTMS('):source.index('static int read_write(')]
tap_code=r'''
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <cassert>
#include <cstdio>
'''+tap+r'''
static tapState_t _state=SHIFT_DR;
static int _num_tms=0,_curr_tms=0,_curr_tdi=0;
static unsigned char _tms_buffer[128]{};
#define JTAG_TMS_BUFFER_SIZE 128
static int hardware=SHIFT_DR;
static const int next_state[16][2]={
 {1,0},{1,2},{3,9},{4,5},{4,5},{6,8},{6,7},{4,8},{1,2},
 {10,0},{11,12},{11,12},{13,15},{13,14},{11,15},{1,2}};
static std::vector<std::array<int,2>> bits;
static std::uint32_t last_gpio=0;
static void edge(int tms){hardware=next_state[hardware][tms];}
struct Gpio {
 void operator=(std::uint32_t value)volatile{
  if(!(last_gpio&2)&&(value&2)){bits.push_back({int(value&1),int((value>>3)&1)});edge(value&1);}
  last_gpio=value;
 }
};
static Gpio gpio;
static volatile Gpio *reg_gpio0_31=&gpio;
static int flushTMS(bool){
 for(int i=0;i<_num_tms;++i)edge((_tms_buffer[i/8]>>(i%8))&1);
 std::memset(_tms_buffer,0,sizeof _tms_buffer);_num_tms=0;return 0;
}
'''+states+writer+r'''
int main(){
 const std::uint8_t bytes[]={0x96,0x03,0x80};
 jtag_writeTDI_msb_first_gpio_out_mode(bytes,2,false);
 assert(_state==SHIFT_DR&&hardware==SHIFT_DR&&bits.size()==16);
 jtag_writeTDI_msb_first_gpio_out_mode(bytes+2,1,true);
 assert(bits.size()==24);
 for(unsigned i=0;i<24;++i){assert(bits[i][1]==((bytes[i/8]>>(7-i%8))&1));assert(bits[i][0]==int(i==23));}
 assert(hardware==EXIT1_DR);
 assert(_state==hardware); // Old fast writer leaves the software TAP in SHIFT_DR.
 set_state(SHIFT_IR);assert(_state==SHIFT_IR&&hardware==SHIFT_IR);
 set_state(RUN_TEST_IDLE);assert(_state==RUN_TEST_IDLE&&hardware==RUN_TEST_IDLE);
 const auto before=bits.size();jtag_writeTDI_msb_first_gpio_out_mode(bytes,0,true);
 assert(bits.size()==before&&_state==hardware);
 std::puts("JTAG TAP PASS: independent 16-state model, MSB stream/chunk boundaries, last-bit exit, next IR/idle transition, empty chunk");
}
'''
loader=source[source.index('bool fpga_program(const char *fname) {'):]
loader_code=r'''
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdio>
using BYTE=unsigned char;using UINT=unsigned;using FRESULT=int;
constexpr int FR_OK=0,FA_READ=1,BLOCK_SIZE=8192,JTAG_MAX_CHAIN=8;
constexpr std::uint32_t IDCODE_GW5AT_60=0x1481b,IDCODE_GWAST_138=0x1081b,IDCODE_GW5A_25=0x1281b,IDCODE_GW2A_18=0x81b;
static int chain_len=0,chain_result=1,critical=0,opened=0,allocations=0,erase_calls=0,read_calls=0,end_calls=0,send_calls=0,end_critical=0;
static bool pins=false,gpio=false,end_ok=true,open_ok=true,start_ok=true,send_ok=true,close_ok=true;
static int erase_fail=0,read_fail=-1,short_read=-1;
static unsigned declared=0,offset=0;
static std::uint32_t idcodes[8]{};
static std::vector<BYTE> data,stream;
static BYTE fbuf[BLOCK_SIZE];static int fcore=0;
static std::uint32_t verify_first_status,verify_status,verify_user,verify_id,poll_mask,poll_value,poll_status;
static unsigned verify_attempts,poll_reads;
void fpga_boot_trace(const char *,const char *){assert(critical==0);}
std::uint32_t get_file_size(const char *){return declared;}
void overlay_status(const char *,...){assert(critical==0);}
void overlay_printf(const char *,...){assert(critical==0);}
#define DEBUG(...) ((void)0)
#define taskENTER_CRITICAL() do {++::critical;}while(0)
#define taskEXIT_CRITICAL() do {assert(::critical>0);--::critical;}while(0)
std::uint64_t bflb_mtimer_get_time_us(){return 123;}
std::uint64_t jtag_writetdi_time=0;
int f_open(int *,const char *,int){if(!open_ok)return 1;++opened;offset=0;return FR_OK;}
int f_read(int *,void *buffer,unsigned length,UINT *count){
 const int call=read_calls++;if(call==read_fail){*count=0;return 1;}
 *count=std::min(length,unsigned(data.size()-offset));if(call==short_read&&*count)--*count;
 std::copy_n(data.data()+offset,*count,static_cast<BYTE *>(buffer));offset+=*count;return FR_OK;
}
int f_close(int *){assert(critical==0);--opened;return close_ok?FR_OK:1;}
void enable_jtag_pins(){assert(!pins);pins=true;}
void disable_jtag_pins(){assert(!gpio&&critical==0);pins=false;}
int detectChain(int){idcodes[0]=IDCODE_GWAST_138;return chain_result;}
bool eraseSRAM(){return ++erase_calls!=erase_fail;}
bool writeSRAM_start(){return start_ok;}
void jtag_enter_gpio_out_mode(){assert(critical==1);gpio=true;}
void jtag_exit_gpio_out_mode(){assert(critical==1&&gpio);gpio=false;}
void jtag_writeTDI_msb_first_gpio_out_mode(const BYTE *p,int n,bool last){assert(gpio);stream.insert(stream.end(),p,p+n);assert(last==(stream.size()==declared));}
bool writeSRAM_send(const BYTE *p,unsigned bits,bool last){++send_calls;stream.insert(stream.end(),p,p+bits/8);assert(last==(stream.size()==declared));return send_ok;}
bool writeSRAM_end(){assert(!gpio);end_critical=critical;++end_calls;return end_ok;}
void *allocate(unsigned n){++allocations;return std::malloc(n);}
void release(void *p){if(p){--allocations;std::free(p);}}
#define malloc allocate
#define free release
'''+loader+r'''
void setup(unsigned n=17001){
 assert(critical==0&&opened==0&&allocations==0&&!pins&&!gpio);
 data.resize(n);for(unsigned i=0;i<n;++i)data[i]=BYTE(i*37+19);
 declared=n;stream.clear();offset=0;chain_result=1;end_ok=open_ok=start_ok=send_ok=close_ok=true;
 erase_fail=erase_calls=read_calls=end_calls=send_calls=0;read_fail=short_read=-1;
}
void clean(){assert(critical==0&&opened==0&&allocations==0&&!pins&&!gpio);}
int main(){
 setup();end_ok=false;assert(!fpga_program("test.bin"));clean(); // Old code leaks the critical section.
 setup();assert(fpga_program("test.bin"));clean();assert(end_calls==1&&end_critical==0);
#ifdef JTAG_FAST
 assert(stream==data);
#else
 for(unsigned i=0;i<data.size();++i){unsigned v=data[i],expected=0;for(int b=0;b<8;++b){expected=(expected<<1)|(v&1);v>>=1;}assert(stream[i]==expected);}
#endif
 for(int call=0;call<3;++call){setup();read_fail=call;assert(!fpga_program("x"));clean();assert(end_calls==0);}
 setup();short_read=0;assert(!fpga_program("x"));clean();assert(end_calls==0);
 setup();++declared;assert(!fpga_program("x"));clean();assert(end_calls==0);
 setup(0);assert(!fpga_program("x"));clean();
 setup();open_ok=false;assert(!fpga_program("x"));clean();
 setup();chain_result=-1;assert(!fpga_program("x"));clean();assert(erase_calls==0);
 for(int e=1;e<=2;++e){setup();erase_fail=e;assert(!fpga_program("x"));clean();}
 setup();start_ok=false;assert(!fpga_program("x"));clean();
 setup();close_ok=false;assert(!fpga_program("x"));clean();
#ifndef JTAG_FAST
 setup();send_ok=false;assert(!fpga_program("x"));clean();assert(end_calls==0);
#endif
 std::puts("FPGA loader PASS: real loader, success, end/read/short/EOF/open/chain/erase/start/close failures, balanced critical/GPIO/file/allocation cleanup");
}
'''
if a.baseline:
    run('programmer_old_'+a.case,tap_code if a.case=='tap' else loader_code)
else:
    run('programmer_tap',tap_code)
    run('programmer_loader',loader_code)
    run('programmer_loader_slow',loader_code.replace('#define JTAG_FAST','// Slow-path regression'))
