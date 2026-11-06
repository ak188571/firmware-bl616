"""Compile the real restart dispatch block, rejecting added loader/storage calls."""
from pathlib import Path
import subprocess,os
root=Path(__file__).resolve().parents[2]
main=(root/'main.cpp').read_text(encoding='utf-8')
block=main[main.index('                        if(system_action==Ui16SystemAction::Restart){'):main.index('                        if(return_to_menu)break;')]
block=block.replace('continue;','return;')
code=r'''
#include <cassert>
#include <cstdio>
#include "ui16_system_osd.h"
static bool core_running,restart_capable,released;
static int active_core,commands,waits,ms,overlay_state,critical_depth;
#define DEBUG(...) ((void)0)
#define taskENTER_CRITICAL() (++critical_depth)
#define taskEXIT_CRITICAL() (--critical_depth)
void ui16_wait_release(){released=true;++waits;}
void fpga_tx_header(int command,int length){assert(released&&critical_depth==1&&command==0x13&&length==1);++commands;}
void delay(unsigned value){ms+=value;}
void overlay(int value){overlay_state=value;}
void dispatch(Ui16SystemAction system_action){
'''+block+r'''
}
int main(){
 for(int core=0;core<7;++core)for(int running=0;running<2;++running)for(int capable=0;capable<2;++capable){
  core_running=running;restart_capable=capable;active_core=core;
  commands=waits=ms=0;released=false;overlay_state=1;
  dispatch(Ui16SystemAction::Restart);
  assert(waits==1&&released&&overlay_state==0);
  const bool expected=running&&capable&&(core==1||core==2);
  assert(commands==int(expected)&&ms==(expected?25:0)&&critical_depth==0);
 }
 commands=waits=0;dispatch(Ui16SystemAction::None);dispatch(Ui16SystemAction::ReturnToList);
 assert(commands==0&&waits==0);
 std::puts("OSD1 dispatch PASS: real main-task block, release before reset, capability/running/core guards, only dedicated UART command");
}
'''
build=root/'build-ui16-host';build.mkdir(exist_ok=True)
source=build/'osd1_dispatch.cpp';source.write_text(code,encoding='utf-8')
compiler=os.environ.get('CXX','g++')
exe=build/'osd1_dispatch.exe'
subprocess.run([compiler,'-BC:/msys64/ucrt64/bin/','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I'+str(root/'ui'),str(source),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
