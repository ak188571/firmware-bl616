#!/usr/bin/env python3
"""Compile the actual OSD writers/capture hooks and config parser with host task/UART substitutes."""
from pathlib import Path
import os,subprocess
ROOT=Path(__file__).resolve().parents[2];build=ROOT/'build-ui16-host';build.mkdir(exist_ok=True)
source=(ROOT/'ui/overlay.cpp').read_text(encoding='utf-8')
a=source.index('// show a pop-up message');b=source.index('// turn overlay on/off',a);source=source[:a]+source[b:]
source=source.replace('extern "C" {\n#include <FreeRTOS.h>\n#include <task.h>\n}','').replace('#include "utils.h"','')
main=(ROOT/'main.cpp').read_text(encoding='utf-8')
parser=main[main.index('            } else if (type == 2)'):main.index('            } else if (type == 3)')]
parser=parser[parser.index('#ifdef UI16_SYSTEM_MENU'):]
query=main[main.index('static bool ui16_hw_osd_ready('):main.index('// UI16 launch adapters BEGIN')]
code=r'''
#include <cstdint>
#include <string>
#include <cstring>
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstdarg>
using TaskHandle_t=void *;
static int task1,task2;
static TaskHandle_t current=&task1;
TaskHandle_t xTaskGetCurrentTaskHandle(){return current;}
static int depth=0;
#define taskENTER_CRITICAL() do {++depth;} while(0)
#define taskEXIT_CRITICAL() do {--depth;} while(0)
static std::vector<unsigned> bytes;
static volatile bool osd_config_ready=false,osd_config_capable=false;
static bool reply=true,supported=true;
void fpga_tx_header(int command,int len){bytes.insert(bytes.end(),{0xaa,unsigned(len>>8),unsigned(len&255),unsigned(command)});if(command==2&&reply){osd_config_ready=true;osd_config_capable=supported;}}
void fpga_tx_byte(unsigned char b){bytes.push_back(b);}
unsigned long long clock_ms=0;
unsigned long long bflb_mtimer_get_time_ms(){return clock_ms;}
void delay(unsigned ms){clock_ms+=ms;}
#define OSD_KEY_CODE 0x84
'''+source+query+r'''
void feed_config(const std::string &s){
    unsigned pos=4,len=s.size()+1;
    char osd_tag[7]={};unsigned osd_tag_count=0;bool osd_tag_found=false;
    for(unsigned char ch:s){
'''+parser+r'''
    }
}
int main(){
    overlay_osd_window(12,1,19,6);assert((bytes==std::vector<unsigned>{0xaa,0,5,0x10,12,1,19,6}));
    bytes.clear();overlay_osd_key(12);assert((bytes==std::vector<unsigned>{0xaa,0,3,0x11,0,12}));
    for(int value:{0,1}){bytes.clear();overlay(value);assert((bytes==std::vector<unsigned>{0xaa,0,5,9,0,0,0,0,0xaa,0,2,8,unsigned(value)}));}
    bytes.clear();overlay_osd_capture_begin();overlay_clear();overlay_cursor(5,9);overlay_printf("PARAM %d",1);
    assert(bytes.empty()&&ui16_osd_canvas().cells[9*32+5]=='P');
    current=&task2;overlay_cursor(0,0);overlay_printf("OTHER TASK");assert(!bytes.empty());
    current=&task1;overlay_osd_capture_end();assert(!ui16_osd_capture_active()&&depth==0);
    osd_config_ready=false;osd_config_capable=false;feed_config("TangCore;OSDWIN1");assert(osd_config_ready&&osd_config_capable);
    osd_config_ready=false;osd_config_capable=true;feed_config("TangCore;OLD");assert(osd_config_ready&&!osd_config_capable);
    supported=true;assert(ui16_hw_osd_ready(nullptr));supported=false;assert(!ui16_hw_osd_ready(nullptr));
    reply=false;clock_ms=0;assert(!ui16_hw_osd_ready(nullptr)&&clock_ms==400);
    std::puts("Actual OSD driver passed: window/key packets, neutral-before-open/close, task-owned capture, marker parser, old-core rejection, bounded capability wait");
}
'''
path=build/'osd_driver_test.cpp';path.write_text(code,encoding='utf-8',newline='\n')
extra=[]
if os.name=='nt':extra=['-B'+subprocess.check_output(['cygpath','-m','/ucrt64/bin'],text=True,encoding='utf-8').strip()+'/']
exe=build/('osd_driver_test.exe' if os.name=='nt' else 'osd_driver_test')
subprocess.run(['g++',*extra,'-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-DUI16_SYSTEM_MENU','-I'+str(ROOT/'ui'),str(path),str(ROOT/'ui/ui16_osd.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
