#!/usr/bin/env python3
"""Run real control/IR/DR/end/poll code against a falling-edge TDO TAP model."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[2];out=root/'build-ui16-host';out.mkdir(exist_ok=True)
s=(root/'fpga/programmer.cpp').read_text(encoding='utf-8')
def section(start,end):return s[s.index(start):s.index(end,s.index(start))]
code=r'''
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <vector>
#define JTAG_TMS_BUFFER_SIZE 128
'''+section('typedef enum tapState_t','// Tang Primer 25K')+r'''
static int hw=EXIT1_DR,ir=0x17,shift_ir=0;static unsigned bit_at=0;
static bool tck=true,tms=true,tdi=false,tdo=false,ready=false;
static uint64_t now=0,changed=0;static bool timer_frozen=false;
static uint32_t expected_id=0x1081b,model_id=expected_id,model_status=0x26020,model_user=0xe613;
static uint32_t idcodes[]={expected_id};static std::vector<int> commands;
static int clocks=0,resets=0;
static const int next[16][2]={{1,0},{1,2},{3,9},{4,5},{4,5},{6,8},{6,7},{4,8},{1,2},
 {10,0},{11,12},{11,12},{13,15},{13,14},{11,15},{1,2}};
void bflb_mtimer_delay_us(unsigned us){now+=us;}
uint64_t bflb_mtimer_get_time_us(){return timer_frozen?0:now;}
void vTaskDelay(unsigned){now+=1000;}
void GPIO_PIN_JTAG_TCK_L(){
 if(tck){assert(now-changed>=1||clocks==0);tck=false;changed=now;}
 // TDO is valid during low phase; a rising edge advances it to the next bit.
 if(hw==SHIFT_DR){const uint32_t v=ir==0x41?(ready?model_status:0):ir==0x11?model_id:ir==0x13?model_user:0;
 tdo=(v>>(bit_at&31))&1;}
}
void GPIO_PIN_JTAG_TMS_W(bool v){assert(!tck);tms=v;}
void GPIO_PIN_JTAG_TDI_W(bool v){assert(!tck);tdi=v;}
int jtag_read_tdo(){assert(!tck&&now-changed>=1);return tdo;}
void GPIO_PIN_JTAG_TCK_H(){
 assert(!tck&&now-changed>=1);tck=true;changed=now;++clocks;
 if(hw==SHIFT_IR)shift_ir|=int(tdi)<<bit_at++;
 if(hw==SHIFT_DR)++bit_at;
 const int n=next[hw][tms];
 if(n==CAPTURE_IR){shift_ir=0;bit_at=0;}
 if(n==CAPTURE_DR)bit_at=0;
 if(n==UPDATE_IR){ir=shift_ir&255;commands.push_back(ir);}
 if(n==TEST_LOGIC_RESET){ir=0x11;++resets;}
 if(n==RUN_TEST_IDLE&&hw==UPDATE_DR)ready=true;
 hw=n;tdo=false;
}
'''+section('static int jtag_clock(', '//         TMS  TCK')+section('static int jtag_writeTDI(', '#define GPIO_INT_MASK')+section('static int flushTMS(', '// isFull()')+section('static unsigned _dr_bits_before', '#define NOOP')+section('#define NOOP', 'bool pollFlag(')+section('bool pollFlag(', 'bool enableCfg()')+section('bool writeSRAM_end()', 'void fpga_boot_trace(')+r'''
void setup(uint32_t status=0x26020,uint32_t id=0x1081b){
 _state=EXIT1_DR;hw=EXIT1_DR;_num_tms=0;std::memset(_tms_buffer,0,sizeof _tms_buffer);
 _curr_tms=1;_curr_tdi=0;tck=true;tms=true;tdi=false;tdo=false;ready=false;ir=0x17;bit_at=0;
 clocks=resets=0;now=changed=0;timer_frozen=false;commands.clear();model_status=status;model_id=id;
}
int main(){
 setup();assert(writeSRAM_end());assert(hw==_state&&hw==RUN_TEST_IDLE);assert(verify_attempts==1);
 assert(verify_first_status==model_status&&verify_id==expected_id&&verify_user==0xe613);
 assert(commands.size()==5&&commands[0]==0x3a&&commands[1]==2&&commands[2]==0x41&&commands[3]==0x11&&commands[4]==0x13);
 for(uint32_t status:{0u,0xffffffffu,0x26021u,0x26022u,0x26024u,0x24020u}){
  setup(status);assert(!writeSRAM_end());assert(verify_attempts==3&&resets>=8&&hw==_state);
 }
 setup(0x26020,0x1481b);assert(!writeSRAM_end());assert(verify_attempts==3);
 setup();assert(writeSRAM_end());assert(pollFlag(0x80,0));
 setup(0);assert(!pollFlag(0x80,0));assert(poll_reads<=513);
 setup(0xffffffffu);assert(!pollFlag(0x80,0x80));assert(poll_reads<=513);
 setup(0x26020);timer_frozen=true;assert(!pollFlag(0x80,0x80));assert(poll_reads==513);
 // Partial-byte receive storage must be cleared as well, and bits stay LSB-first.
 setup();go_test_logic_reset();uint32_t tx=0xffffffff,rx=0;shiftDR((uint8_t*)&tx,(uint8_t*)&rx,32);assert(rx==expected_id);
 std::puts("JTAG control PASS: real clocks/IR/DR/end, low-phase TDO, idle completion, ordered reads, valid DONE+ID, error/zero/ones/wrong ID rejection, three attempts, bounded polling");
}
'''
p=out/'jtag_control.cpp';p.write_text(code,encoding='utf-8',newline='\n')
env=os.environ.copy();env['MSYS2_ARG_CONV_EXCL']='*'
exe=out/('jtag_control.exe' if os.name=='nt' else 'jtag_control')
extra=['-BC:/msys64/ucrt64/bin/'] if os.name=='nt' else []
subprocess.run(['g++',*extra,'-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-sign-compare',str(p),'-o',str(exe)],check=True,env=env)
subprocess.run([str(exe)],check=True)

code=r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <cassert>
#define UI16_SYSTEM_MENU
using UINT=unsigned;struct FIL{};
constexpr int FR_OK=0,FA_OPEN_ALWAYS=1,FA_WRITE=2;
const char *drv="sd:";static std::string contents="existing\n";
static bool open_ok=true,seek_ok=true,write_ok=true,sync_ok=true,short_write=false;
static int opened=0,syncs=0;static unsigned position=0;
static uint32_t verify_id=0x1081b,verify_first_status=0,verify_status=0x26020,verify_user=0xe613;
static unsigned verify_attempts=2,poll_reads=3;static uint32_t poll_mask=0x80,poll_value=0,poll_status=0x26020;
uint64_t bflb_mtimer_get_time_us(){return 1000;}
int f_open(FIL *,const char *p,int flags){assert(std::string(p)=="sd:/crt-boot.log"&&flags==3);if(!open_ok)return 1;++opened;return 0;}
unsigned f_size(FIL *){return contents.size();}
int f_lseek(FIL *,unsigned p){position=p;return seek_ok?0:1;}
int f_write(FIL *,const void *p,unsigned n,UINT *written){
 assert(position==contents.size());*written=short_write?n-1:n;if(!write_ok)return 1;
 contents.append(static_cast<const char *>(p),*written);return 0;
}
int f_sync(FIL *){++syncs;return sync_ok?0:1;}
int f_close(FIL *){--opened;return 0;}
'''+section('void fpga_boot_trace(', 'void fpgaStatus()')+r'''
int main(){
 fpga_boot_trace("program-ok","core.bin");assert(opened==0&&syncs==1&&contents.find("existing\n")==0);
 assert(contents.find("phase=program-ok id=0001081b first=00000000 status=00026020 user=0000e613 tries=2")!=std::string::npos);
 contents.assign(65535,'x');fpga_boot_trace("full","no write");assert(contents.size()==65535&&opened==0);
 contents="";open_ok=false;fpga_boot_trace("open-fail","");assert(opened==0&&contents.empty());open_ok=true;
 seek_ok=false;fpga_boot_trace("seek-fail","");assert(opened==0&&contents.empty());seek_ok=true;
 write_ok=false;fpga_boot_trace("write-fail","");assert(opened==0&&contents.empty());write_ok=true;
 const auto before=syncs;short_write=true;fpga_boot_trace("short-write","");assert(opened==0&&syncs==before);short_write=false;
 sync_ok=false;fpga_boot_trace("sync-fail","");assert(opened==0);
 drv="invalid";fpga_boot_trace("invalid","");assert(opened==0);
 std::puts("Boot trace PASS: actual bounded append, existing data retained, fields, open/seek/write/short/sync failures, invalid volume");
}
'''
p=out/'boot_trace.cpp';p.write_text(code,encoding='utf-8',newline='\n')
exe=out/('boot_trace.exe' if os.name=='nt' else 'boot_trace')
subprocess.run(['g++',*extra,'-std=c++17','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True,env=env)
subprocess.run([str(exe)],check=True)
