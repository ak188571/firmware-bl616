"""Compile the actual UART task and storage lease, injecting packets/interleavings only."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[2]
source=(root/'main.cpp').read_text(encoding='utf-8')
ownership=source.split('// Application storage ownership BEGIN',1)[1].split('\n',1)[1].split('// Application storage ownership END.',1)[0]
task=source[source.index('static void uart1_rx_task('):source.index('\n#ifdef UI16_SYSTEM_MENU\nstatic bool ui16_hw_osd_ready')]
code=r'''
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
extern "C" {
#include "ff.h"
}
using SemaphoreHandle_t=int;
constexpr int pdTRUE=1,portMAX_DELAY=-1;
#define pdMS_TO_TICKS(n) (n)
static bool core_running=false,retire_on_lease=false;
static int active_core=-1,held=0,core_id=0,state_mutex=2;
static uint16_t joy1_state=0,joy2_state=0;
static bool osd_config_ready=false,osd_config_capable=false,crt_stage1_capable=false,crt_blur_capable=false,crt_scanline_capable=false,restart_capable=false;
static bool floppy[2]={true,true};
static FIL f_floppy[2];
alignas(64) static uint8_t fbuf[8192];
static unsigned reads=0,writes=0,replies=0,reply_bytes=0,errors=0,statuses=0;
static bool seek_fail=false,short_read=false;
int xSemaphoreTake(int which,int){if(which==1){assert(!held);held=1;if(retire_on_lease){active_core=2;core_running=false;}}return 1;}
void xSemaphoreGive(int which){if(which==1){assert(held);held=0;}}
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
void ui16_crt_accept_status(uint32_t value){assert(value==0x001f1234);statuses++;}
void fpga_tx_header(int command,int length){assert(held&&command==0x0a&&length==513);replies++;}
void fpga_tx_byte(uint8_t){assert(held);reply_bytes++;}
void overlay_status(const char *,...){errors++;}
extern "C" FRESULT f_lseek(FIL *,FSIZE_t){assert(held);return seek_fail?FR_DISK_ERR:FR_OK;}
extern "C" FRESULT f_write(FIL *,const void *data,UINT size,UINT *count){assert(held&&data==fbuf&&size==512);writes++;*count=size;return FR_OK;}
extern "C" FRESULT f_read(FIL *,void *data,UINT size,UINT *count){assert(held&&data==fbuf&&size==512);reads++;*count=short_read?511:512;std::memset(data,0x77,*count);return FR_OK;}
static std::vector<uint8_t> wire;
static unsigned cursor=0,switch_at=~0u,first_packet_end=0;
int uart1_rx_take(uint8_t *byte){
    if(cursor==switch_at){active_core=2;core_running=false;}
    if(cursor<first_packet_end)for(auto value:fbuf)assert(value==0x5a); // No streamed write to shared buffer.
    if(cursor==wire.size())return 0;
    *byte=wire[cursor++];return 1;
}
struct Done{};
void vTaskDelay(unsigned){throw Done{};}
'''+ownership+task+r'''
void packet(unsigned type,const std::vector<uint8_t> &payload,unsigned override_length=0){
    const auto len=override_length?override_length:unsigned(payload.size()+1);
    wire.insert(wire.end(),{0xaa,uint8_t(len>>8),uint8_t(len),uint8_t(type)});
    wire.insert(wire.end(),payload.begin(),payload.end());
}
void reset(int core,bool running){
    active_core=core;core_running=running;retire_on_lease=seek_fail=short_read=false;
    storage_mutex=1;held=0;wire.clear();cursor=0;switch_at=~0u;first_packet_end=0;
    reads=writes=replies=reply_bytes=errors=statuses=0;joy1_state=joy2_state=0;
    std::memset(fbuf,0x5a,sizeof fbuf);
}
void tail(){packet(3,{0x12,0x34,0x56,0x78});packet(0x12,{0x00,0x1f,0x12,0x34});}
void run(){try{uart1_rx_task(nullptr);}catch(Done &){}assert(cursor==wire.size()&&!held);}
int main(){
    std::vector<uint8_t> data(514,0xaa);data[0]=0;data[1]=3;
    for(int core=-1;core<=6;core++)for(bool running:{false,true}){
        reset(core,running);packet(4,data);first_packet_end=unsigned(wire.size());packet(5,{0,3});tail();run();
        assert(joy1_state==0x1234&&joy2_state==0x5678&&statuses==1);
        if(core==6&&running){assert(writes==1&&reads==1&&replies==1&&reply_bytes==512);}
        else{assert(reads==0&&writes==0&&replies==0);for(auto b:fbuf)assert(b==0x5a);}
    }
    // Retire PC/XT during the payload, or while waiting to acquire its lease.
    for(bool during_packet:{false,true}){
        reset(6,true);packet(4,data);first_packet_end=unsigned(wire.size());tail();
        if(during_packet)switch_at=100;else retire_on_lease=true;
        run();assert(reads==0&&writes==0);for(auto b:fbuf)assert(b==0x5a);
    }
    // A known-length rejected packet consumes embedded AA bytes and preserves following joystick/status packets.
    for(unsigned bad_length:{1u,3u,514u,516u}){
        reset(2,true);packet(4,std::vector<uint8_t>(bad_length-1,0xaa),bad_length);tail();run();
        assert(reads==0&&writes==0&&joy1_state==0x1234&&statuses==1);
    }
    reset(6,true);seek_fail=true;packet(4,data);packet(5,{0,3});tail();run();assert(writes==0&&reads==0&&errors==2&&replies==0);
    reset(6,true);short_read=true;packet(5,{0,3});tail();run();assert(reads==1&&errors==1&&replies==0);
    std::puts("Storage UART PASS: real decoder/lease, all cores/running guards, no partial-packet fbuf writes, owner changes, rejected payload resync, exact PC disk I/O and short/seek failure");
}
'''
build=root/'build-ui16-host';build.mkdir(exist_ok=True)
path=build/'storage_uart_test.cpp';path.write_text(code,encoding='utf-8')
exe=build/'storage_uart_test.exe'
subprocess.run([os.environ.get('CXX','g++'),'-BC:/msys64/ucrt64/bin/','-std=c++17','-Wall','-Wextra','-Werror',
    '-Wno-unused-parameter','-pedantic','-DUI16_SYSTEM_MENU','-I'+str(root/'tests/ui16'),str(path),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
