#!/usr/bin/env python3
"""Compile the actual UART IRQ, ring and complete RX task with a bounded FIFO model."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[2];build=root/'build-ui16-host';build.mkdir(exist_ok=True)
s=(root/'main.cpp').read_text(encoding='utf-8')
a=s.index('// UART1 RX IRQ');b=s.index('\n#ifdef UI16_SYSTEM_MENU\nstatic bool ui16_hw_osd_ready',a)
actual=s[a:b]
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>
#include <string>
#include <cstdio>
using UINT=unsigned;struct FIL{};FIL f_floppy[2];bool floppy[2]={};uint8_t fbuf[512];
constexpr int FR_OK=0;int f_lseek(FIL*,unsigned){return 0;}
int f_write(FIL*,void*,unsigned,UINT*){return 0;}int f_read(FIL*,void*,unsigned,UINT*){return 1;}
void overlay_status(const char*){}void fpga_tx_header(int,int){}void fpga_tx_byte(uint8_t){}
struct Device{int irq_num=22;} device;Device *uart1_dev=&device;
static bool osd_config_ready=false,osd_config_capable=false;
static int depth=0;static unsigned core_id=0,joy1_state=0,joy2_state=0;void *state_mutex=nullptr;
#define pdTRUE 1
#define portMAX_DELAY 0
int xSemaphoreTake(void*,int){return 1;}void xSemaphoreGive(void*){}
#define taskENTER_CRITICAL() do{++depth;}while(0)
#define taskEXIT_CRITICAL() do{assert(--depth==0);}while(0)
#define pdMS_TO_TICKS(x) (x)
constexpr unsigned UART_INTSTS_RTO=16,UART_INTCLR_RTO=16;
static std::deque<uint8_t> fifo;static unsigned dropped=0;
unsigned bflb_uart_get_intstatus(Device*){return UART_INTSTS_RTO;}
void bflb_uart_int_clear(Device*,unsigned){}
unsigned bflb_uart_rxavailable(Device*){return fifo.size();}
uint8_t bflb_uart_getchar(Device*){assert(!fifo.empty());auto b=fifo.front();fifo.pop_front();return b;}
using Handler=void(*)(int,void*);Handler handler=nullptr;static bool enabled=false,unmasked=false;
void bflb_irq_attach(int,Handler h,void*){handler=h;}
void bflb_uart_rxint_mask(Device*,bool mask){unmasked=!mask;}
void bflb_irq_enable(int){enabled=true;}
void feed(const std::vector<uint8_t>&v,bool irq=true){for(auto b:v){if(fifo.size()<32)fifo.push_back(b);else ++dropped;if(irq&&fifo.size()>=8)handler(22,nullptr);}if(irq&&!fifo.empty())handler(22,nullptr);}
struct Done{};static std::deque<std::vector<uint8_t>> later;static unsigned sleeps=0;
void vTaskDelay(unsigned){++sleeps;if(later.empty())throw Done{};auto v=later.front();later.pop_front();feed(v);}
'''+actual+r'''
void parse(){try{uart1_rx_task(nullptr);}catch(const Done&){}assert(depth==0);}
int main(){
 uart1_rx_start();assert(handler&&enabled&&unmasked);
 const std::string text="Tangcores;-;O12,OSD key,Right+Select,Select+Start,Select+RB;-;V,v20240101;OSDWIN1";
 std::vector<uint8_t> packet={0xaa,0,82,2};packet.insert(packet.end(),text.begin(),text.end());assert(packet.size()==85);
 // Existing polling cannot hold even one complete config response in the hardware FIFO.
 feed(packet,false);assert(dropped==53&&fifo.size()==32);fifo.clear();dropped=0;
 feed(packet);parse();assert(!dropped&&osd_config_ready&&osd_config_capable&&sleeps==1);
 // A fragmented response preserves parser state across scheduler waits.
 osd_config_ready=osd_config_capable=false;sleeps=0;
 feed({packet.begin(),packet.begin()+13});later.push_back({packet.begin()+13,packet.end()});
 later.push_back({0xaa,0,5,3,0x12,0x34,0x56,0x78,0xaa,0,2,1,1});parse();
 assert(osd_config_ready&&osd_config_capable&&joy1_state==0x1234&&joy2_state==0x5678&&core_id==1);
 // Existing 515-byte floppy responses require a position counter wider than eight bits.
 std::vector<uint8_t> disk={0xaa,2,3,4,0,0};for(unsigned i=0;i<512;++i)disk.push_back(i);
 feed(disk);parse();for(unsigned i=0;i<512;++i)assert(fbuf[i]==uint8_t(i));
 // Ring wrap, overflow reporting, and recovery with a fresh packet.
 for(int i=0;i<25;++i){feed(packet);parse();assert(osd_config_capable);}
 feed(std::vector<uint8_t>(1100,0x55));parse();assert(!uart1_rx_overflow);
 feed({0xaa,0,2,1,2});parse();assert(core_id==2);
 osd_config_ready=osd_config_capable=false;feed({0xaa,0,4,2,'O','L','D'});parse();assert(osd_config_ready&&!osd_config_capable);
 std::puts("UART RX passed: 32-byte FIFO reproducer, complete 85-byte reply, IRQ registration, fragmented/mixed frames, 515-byte frame, ring wrap/overflow/recovery, old-core rejection");
}
'''
p=build/'uart_rx_test.cpp';p.write_text(code,encoding='utf-8',newline='\n');extra=[]
if os.name=='nt':extra=['-B'+subprocess.check_output(['cygpath','-m','/ucrt64/bin'],text=True,encoding='utf-8').strip()+'/']
exe=build/('uart_rx_test.exe' if os.name=='nt' else 'uart_rx_test')
subprocess.run(['g++',*extra,'-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DUI16_SYSTEM_MENU',str(p),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
