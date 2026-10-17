#!/usr/bin/env python3
"""Compile the actual main.cpp launch/HID adapters with host hardware substitutes."""
from pathlib import Path
import os,subprocess
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'main.cpp').read_text(encoding='utf-8')
adapters=source.split('// UI16 launch adapters BEGIN',1)[1].split('\n',1)[1].split('// UI16 launch adapters END',1)[0]
hid=source[source.index('static void send_hid_to_core(void)'):source.index('// // (R L X A RT LT DN UP START SELECT Y B)')]
build=ROOT/'build-ui16-host';build.mkdir(exist_ok=True)
code=r'''
#include <cstdint>
#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
using std::uint16_t;using std::uint64_t;
struct FILINFO {unsigned char fattrib=0;unsigned fsize=100;};
constexpr int FR_OK=0,AM_DIR=16;
static int file_result=0;static bool directory=false,program_ok=true,owner=true;
uint16_t joy1_state=0xffff,joy2_state=0xffff;
#define taskENTER_CRITICAL() do {} while(0)
#define taskEXIT_CRITICAL() do {} while(0)
int f_stat(const char *,FILINFO *p){p->fattrib=directory?AM_DIR:0;return file_result;}
bool find_core_for_board(std::string &p,const char *n){p=std::string("sd:cores/")+n;return true;}
bool core_running=true,gba_bios_loaded=true,gba_missing_bios_warned=true;int active_core=1,_overlay_on=1;
bool fpga_program(const char *){return program_ok;}
static uint64_t now=0;static int id=1,requests=0;
uint64_t bflb_mtimer_get_time_ms(){return now;}
void delay(unsigned ms){now+=ms;}
void send_blank_packet(){}
int get_core_id(){++requests;now+=200;return id;}
static int result=0;static bool started=true;
int rom(const char *){core_running=started;return result;}
struct core_info {int (*load_rom)(const char *);};
core_info target{rom};
core_info *find_core_by_id(int n){return n==1?&target:nullptr;}
void overlay(int n){_overlay_on=n;}
void overlay_osd_window(unsigned,unsigned,unsigned,unsigned){}
int overlay_on(){return _overlay_on;}
void fpga_use_ui16_display(bool n){owner=n;}
void dprint(const char *,...){}
struct Pad {uint16_t j1=0,j2=0,h1=0,h2=0;};
static std::vector<Pad> pads;static unsigned index=0;
void get_joypad_states(uint16_t *j1,uint16_t *j2,uint16_t *h1,uint16_t *h2){assert(index<pads.size());const auto p=pads[index];*j1=p.j1;*j2=p.j2;*h1=p.h1;*h2=p.h2;}
void vTaskDelay(unsigned){++index;}
#define pdMS_TO_TICKS(n) (n)
#define OSD_KEY_CODE 0x84
static std::vector<unsigned> bytes;
void fpga_tx_header(int c,int n){assert(c==9&&n==5);}
void fpga_tx_byte(unsigned char b){bytes.push_back(b);}
'''+adapters+hid+r'''
int main(){
 assert(ui16_hw_file_ready("sd:a.nes",nullptr));directory=true;assert(!ui16_hw_file_ready("x",nullptr));directory=false;
 file_result=1;assert(!ui16_hw_file_ready("x",nullptr));file_result=0;
 std::string path;assert(ui16_hw_find_core("nestang.bin",path,nullptr)&&path=="sd:cores/nestang.bin");
 assert(ui16_hw_program("x",nullptr));assert(!core_running&&active_core==-1&&!gba_bios_loaded&&!gba_missing_bios_warned&&_overlay_on==1&&joy1_state==0&&joy2_state==0);
 program_ok=false;assert(!ui16_hw_program("x",nullptr));program_ok=true;
 id=1;assert(ui16_hw_wait_core(1,nullptr)&&active_core==1);
 now=0;requests=0;id=2;assert(!ui16_hw_wait_core(1,nullptr)&&now>=2000&&requests<=10);
 started=true;result=0;assert(ui16_hw_load_rom(1,"sd:nes/a.nes",nullptr)&&!overlay_on());
 result=1;assert(!ui16_hw_load_rom(1,"x",nullptr)&&!core_running);
 result=0;started=false;assert(!ui16_hw_load_rom(1,"x",nullptr)); // Includes SNES invalid-header return-0 case.
 assert(!ui16_hw_load_rom(999,"x",nullptr));
 ui16_hw_display_owner(true,nullptr);assert(owner&&overlay_on());
 ui16_hw_display_owner(false,nullptr);assert(!owner&&overlay_on());
 overlay(0);pads={{0,0,0x0200,0},{0,0,0x0200,0},{0,0,0x0002,0},{0,0,0,0},{0,0,0x84,0}};
 send_hid_to_core();assert((bytes==std::vector<unsigned>{2,0,0,0,0,2,0,0,0,0,0,0}));
 // Split chords from two controllers must not open the OSD.
 index=0;bytes.clear();overlay(0);pads={{4,128,0,0},{0x84,0,0,0}};send_hid_to_core();assert(index==1&&bytes.size()==4);
 // Software/keyboard overlay-on also exits gameplay input forwarding.
 index=0;bytes.clear();pads={{}};overlay(1);send_hid_to_core();assert(index==0);
 std::puts("Actual main.cpp adapters passed: stat/program/ID timeout/load state, display ownership, unmodified X/Y game HID, OSD chord, neutral packets");
}
'''
path=build/'launch_adapters_test.cpp';path.write_text(code,encoding='utf-8',newline='\n')
extra=[]
if os.name=='nt':
    converted=subprocess.check_output(['cygpath','-m','/ucrt64/bin'],text=True,encoding='utf-8').strip()
    extra=['-B'+converted+'/']
exe=build/('launch_adapters_test.exe' if os.name=='nt' else 'launch_adapters_test')
subprocess.run(['g++',*extra,'-DUI16_SYSTEM_MENU','-std=c++17','-Wall','-Wextra','-Werror','-pedantic',str(path),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)


# Check the existing PC image path too: an invalid mount must not be treated as a successful start.
pc=(ROOT/'core/pcxt.cpp').read_text(encoding='utf-8')
pc=pc[pc.index('bool mount_floppy('):pc.index('// PcxtMenu implementation')]
pc_code=r"""
#include <string>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
using std::uint16_t;using UINT=unsigned;using FRESULT=int;
constexpr int FR_OK=0,FA_READ=1,FA_WRITE=2;
struct FIL{};struct FILINFO{unsigned fsize;};
FIL fcore,f_floppy[2];bool floppy[2]={},core_running=false;std::string floppy_fname[2];const char *drv="sd:";
static unsigned image_size=360*1024;static bool exists=true,open_ok=true;static int closes=0,overlay_state=1;
int f_stat(const char *p,FILINFO *f){f->fsize=std::string(p).find("bios.bin")!=std::string::npos?1024:image_size;return exists?0:1;}
int f_open(FIL *,const char *,int){return open_ok?0:1;}
int f_read(FIL *,void *,unsigned,unsigned *n){*n=0;return 0;}
int f_close(FIL *){++closes;return 0;}
const char *host_strcasestr(const char *a,const char *b){return std::strstr(a,b);}
#define strcasestr host_strcasestr
void overlay_message(const char *,int){}
void overlay(int n){overlay_state=n;}
void set_loading_state(int){}
void send_fbuf_data(unsigned){}
void IOWR(uint16_t,uint16_t){}
char fbuf[1024];
#define DEBUG(...) ((void)0)
"""+pc+r"""
int main(){
 assert(mount_floppy(0,"sd:pc/a.img")&&floppy[0]);
 exists=false;assert(!mount_floppy(0,"sd:pc/missing.img")&&!floppy[0]);exists=true;
 image_size=1234;const auto before=closes;assert(!mount_floppy(0,"sd:pc/bad.img")&&!floppy[0]&&closes==before+1);
 core_running=false;assert(loadpc("sd:pc/bad.img")==-1&&!core_running);
 image_size=360*1024;assert(loadpc("sd:pc/a.img")==0&&core_running&&!overlay_state);
 std::puts("Actual PC image path passed: failed mount/unsupported size rejected, old handle closed, valid image starts");
}
"""
path=build/'launch_pc_test.cpp';path.write_text(pc_code,encoding='utf-8',newline='\n')
exe=build/('launch_pc_test.exe' if os.name=='nt' else 'launch_pc_test')
subprocess.run(['g++',*extra,'-DUI16_SYSTEM_MENU','-std=c++17','-Wall','-Wextra','-Werror','-pedantic',str(path),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)

# Compile the actual UI16 main-task branch as well, including its popup/resume loop.
branch=source[source.index('        // Keep the configured order;'):source.index('\n#else\n        (void)monitor_loaded;')]
syntax=r'''
#include "ui16_browser.h"
#include "ui16_launch.h"
#include "menu_manager.h"
#include <cstdint>
#include <memory>
#include <vector>
using uint16_t=std::uint16_t;using uint32_t=std::uint32_t;
struct core_info {std::uint16_t id;const char *display_name,*rom_dir,*core_file;int (*load_rom)(const char *);Menu *(*create_menu)(const char *);};
extern std::vector<int16_t> main_menu_config;extern const char *drv;
core_info *find_core_by_id(int);
bool ui16_hw_file_ready(const char *,void *);
bool ui16_hw_find_core(const char *,std::string &,void *);
bool ui16_hw_program(const char *,void *);
bool ui16_hw_wait_core(int,void *);
bool ui16_hw_load_rom(int,const char *,void *);
void ui16_hw_display_owner(bool,void *);
Ui16GameList ui16_load_for_core(int,const char *,void *);
void fpga_use_ui16_display(bool);
bool ui16_hw_osd_ready(void *);
void overlay_osd_window(unsigned,unsigned,unsigned,unsigned);
void ui16_osd_menu_input_loop();
void delay(unsigned);
void get_joypad_states(uint16_t *,uint16_t *,uint16_t *,uint16_t *);
unsigned long long bflb_mtimer_get_time_ms();
void ui16_wait_release();void ui16_restore_menu(Ui16Runtime &,Ui16Browser &);void send_hid_to_core();
Menu *create_default_menu(const char *);
void run_ui16_main(){
'''+branch+'\n}\n'
path=build/'launch_main_syntax.cpp';path.write_text(syntax,encoding='utf-8',newline='\n')
subprocess.run(['g++',*extra,'-DUI16_SYSTEM_MENU','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-pedantic','-fsyntax-only','-I'+str(ROOT/'ui'),str(path)],check=True)
print('Actual UI16 main-task branch syntax passed')
