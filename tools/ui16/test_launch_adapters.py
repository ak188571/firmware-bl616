#!/usr/bin/env python3
"""Compile the actual main.cpp launch/HID adapters with host hardware substitutes."""
from pathlib import Path
import os,subprocess
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'main.cpp').read_text(encoding='utf-8')
adapters=source.split('// UI16 launch adapters BEGIN',1)[1].split('\n',1)[1].split('// UI16 launch adapters END',1)[0]
ownership=source.split('// Application storage ownership BEGIN',1)[1].split('\n',1)[1].split('static bool pcxt_disk_owner()',1)[0]
hid=source[source.index('static void send_hid_to_core(void)'):source.index('// // (R L X A RT LT DN UP START SELECT Y B)')]
start=source.index('                const auto error=runtime.launch(')
handoff=source[start:source.index('                    for(;;){',start)]+'                }\n'
build=ROOT/'build-ui16-host';build.mkdir(exist_ok=True)
code=r'''
#include <cstdint>
#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "ui16_crt.h"
#include "ui16_launch.h"
#include "tc_snes_sd_guard.h"
using SemaphoreHandle_t=int;
constexpr int pdTRUE=1,portMAX_DELAY=-1;
static int storage_depth=0;
static bool lease_available=true,sd_scope=false,sd_fault=false;
static int fail_io_stage=0;
int xSemaphoreTake(int,int){if(!lease_available)return 0;assert(storage_depth==0);storage_depth++;return 1;}
void xSemaphoreGive(int){assert(storage_depth==1);storage_depth--;}
bool tc_snes_sd_scope(bool value){const bool old=sd_scope;sd_scope=value;return old;}
bool tc_snes_sd_faulted(){return sd_fault;}
static bool crt_stage1_capable=false,restart_capable=false;
static Ui16GameOsd game_osd_owner=Ui16GameOsd::System;
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
static unsigned starting_frames=0,tx_waits=0;
struct FIL{};static FIL fcore;
constexpr unsigned BLOCK_SIZE=8192;
static unsigned char fbuf[BLOCK_SIZE];
static unsigned identify_calls=0,rom_calls=0;
static unsigned restore_calls=0,defaults_calls=0;
static bool identify_ok=true;
static std::vector<std::string> io_order;
Ui16RomIdentity ui16_identify_rom(int system,const char *path,FIL &file,void *buffer,unsigned capacity,void (*yield)()){
 assert((system==1||system==2)&& &file==&fcore&&buffer==fbuf&&capacity==BLOCK_SIZE&&!core_running&&storage_depth==1);
 ++identify_calls;io_order.push_back(std::string("identify:")+path);if(yield)yield();
 if(fail_io_stage==1)sd_fault=true;
 return identify_ok?Ui16RomIdentity{true,Ui16RomVolume::Sd,unsigned(system),0xabcdef01,65552}:Ui16RomIdentity{};
}
Ui16CrtRestore ui16_read_crt_override(const Ui16RomIdentity &identity,FIL &file,void *buffer,unsigned capacity){
 assert(&file==&fcore&&buffer==fbuf&&capacity==BLOCK_SIZE&&!core_running);
 ++restore_calls;io_order.push_back("restore");
 if(fail_io_stage==2)sd_fault=true;
 return identity.valid?Ui16CrtRestore{Ui16CrtReadState::User,{true,{true,3,3}}}:Ui16CrtRestore{};
}
Ui16CrtDefaults ui16_read_crt_defaults(int system,const char *path,FIL &file,void *buffer,unsigned capacity){
 assert((system==1||system==2)&&path&& &file==&fcore&&buffer==fbuf&&capacity==BLOCK_SIZE&&!core_running&&storage_depth==1);
 ++defaults_calls;io_order.push_back("defaults");
 if(fail_io_stage==3)sd_fault=true;
 return {Ui16CrtDefaultsState::Ready,Ui16CrtDefaultsSource::System,{true,{true,2,1}}};
}
static unsigned save_calls=0;static bool saved_reset=false;
bool ui16_save_crt_override(const Ui16RomIdentity &,const Ui16CrtSettings &,FIL &file,void *buffer,unsigned capacity,bool reset=false){
 assert(&file==&fcore&&buffer==fbuf&&capacity==BLOCK_SIZE);++save_calls;saved_reset=reset;return true;
}
void overlay_status(const char *,...){}
static int tx_done=0,uart_device=0;
static int *uart1_dev=&uart_device;
void ui16_send_starting_game(){++starting_frames;}
int bflb_uart_wait_tx_done(int *dev){assert(dev==uart1_dev);++tx_waits;return tx_done;}
void send_blank_packet(){}
int get_core_id(){++requests;now+=200;return id;}
static int result=0;static bool started=true;
int rom(const char *){++rom_calls;io_order.push_back("rom");core_running=started;return result;}
struct core_info {int (*load_rom)(const char *);};
core_info target{rom};
core_info *find_core_by_id(int n){return (n==1||n==2)?&target:nullptr;}
void overlay(int n){_overlay_on=n;}
void overlay_osd_window(unsigned,unsigned,unsigned,unsigned){}
void overlay_osd_neutral();
int overlay_on(){return _overlay_on;}
void fpga_use_ui16_display(bool n){owner=n;}
void dprint(const char *,...){}
struct Pad {uint16_t j1=0,j2=0,h1=0,h2=0;};
static std::vector<Pad> pads;static unsigned index=0;
static unsigned neutral_calls=0;
void overlay_osd_neutral(){++neutral_calls;++index;}
void get_joypad_states(uint16_t *j1,uint16_t *j2,uint16_t *h1,uint16_t *h2){assert(index<pads.size());const auto p=pads[index];*j1=p.j1;*j2=p.j2;*h1=p.h1;*h2=p.h2;}
void vTaskDelay(unsigned){++index;}
#define pdMS_TO_TICKS(n) (n)
#define OSD_KEY_CODE 0x84
static std::vector<unsigned> bytes;
void fpga_tx_header(int c,int n){assert(c==9&&n==5);}
void fpga_tx_byte(unsigned char b){bytes.push_back(b);}
'''+ownership+adapters+hid+r'''
static unsigned session_calls=0,preview_calls=0,readonly_calls=0,releases=0;
void ui16_crt_preview_session(){++preview_calls;}
static Ui16CrtRecommendation captured;
static Ui16RomIdentity captured_id;
static Ui16CrtRestore captured_restore;
static Ui16CrtDefaults captured_defaults;
void ui16_wait_release(){++releases;}
void ui16_crt_new_session(const Ui16CrtRecommendation &value,const Ui16RomIdentity &identity,const Ui16CrtRestore &restore,Ui16CrtSaveFn save,const Ui16CrtDefaults &defaults){assert(save==ui16_hw_save_crt);++session_calls;captured=value;captured_id=identity;captured_restore=restore;captured_defaults=defaults;}
void ui16_crt_readonly_session(const Ui16CrtRecommendation &value,const Ui16RomIdentity &identity,const Ui16CrtRestore &restore,const Ui16CrtDefaults &defaults){assert(identity.system==2);++readonly_calls;captured=value;captured_id=identity;captured_restore=restore;captured_defaults=defaults;}
struct FakeLaunch {
 Ui16LaunchError result;
 Ui16LaunchError launch(const char *,const Ui16LaunchTarget &,const Ui16LaunchRequest &){return result;}
};
void launch_handoff(Ui16LaunchError result,const Ui16LaunchRequest &request){
 FakeLaunch runtime{result};const char *drv="sd:";Ui16LaunchTarget launch_target{1,"nes","nestang.bin"};
'''+handoff+r'''
}
'''+r'''
int main(){
 storage_mutex=1;
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
 assert(identify_calls==0&&restore_calls==0&&defaults_calls==0&&!crt_rom_identity.valid); // Unsupported cores have no extra file access.
 crt_stage1_capable=true;started=true;result=0;
 const auto before_snes_identify=identify_calls,before_snes_restore=restore_calls,before_snes_defaults=defaults_calls;
 assert(ui16_hw_load_rom(2,"sd:snes/a.sfc",nullptr));
 assert(identify_calls==before_snes_identify+1&&restore_calls==before_snes_restore+1&&defaults_calls==before_snes_defaults+1&&crt_rom_identity.system==2&&snes_crt_readonly&&!sd_scope&&storage_depth==0);
 const auto before_usb=identify_calls;
 assert(ui16_hw_load_rom(2,"usb:snes/a.sfc",nullptr)&&identify_calls==before_usb&&!crt_rom_identity.valid&&!snes_crt_readonly);
 for(int stage=1;stage<=3;stage++){
  fail_io_stage=stage;sd_fault=false;const auto before_rom=rom_calls;
  assert(!ui16_hw_load_rom(2,"sd:snes/fail.sfc",nullptr)&&rom_calls==before_rom&&!core_running&&!crt_rom_identity.valid&&!snes_crt_readonly);
  assert(!crt_restore.override_value.present&&!crt_defaults.recommendation.present&&!sd_scope&&storage_depth==0);
 }
 fail_io_stage=0;sd_fault=false; // Substitute power-on reset, not a firmware recovery API.
 crt_stage1_capable=true;started=true;result=0;io_order.clear();
 assert(ui16_hw_load_rom(1,"sd:nes/id.nes",nullptr)&&crt_rom_identity.valid);
 assert((io_order==std::vector<std::string>{"identify:sd:nes/id.nes","restore","defaults","rom"}));
 assert(crt_restore.override_value.present&&crt_defaults.recommendation.present);
 identify_ok=false;const auto before_loads=rom_calls;
 assert(ui16_hw_load_rom(1,"sd:nes/noid.nes",nullptr)&&!crt_rom_identity.valid&&rom_calls==before_loads+1);
 assert(!crt_restore.override_value.present);
 identify_ok=true;result=1;assert(!ui16_hw_load_rom(1,"x",nullptr)&&!crt_rom_identity.valid&&!crt_defaults.recommendation.present);
 result=0;crt_stage1_capable=false;
 ui16_hw_display_owner(true,nullptr);assert(owner&&overlay_on());
 ui16_hw_display_owner(false,nullptr);assert(!owner&&overlay_on());
 const auto before_notice=now;
 assert(ui16_hw_starting(nullptr)&&starting_frames==1&&tx_waits==1&&now==before_notice+500);
 tx_done=-1;
 assert(!ui16_hw_starting(nullptr)&&starting_frames==2&&tx_waits==2&&now==before_notice+500);
 overlay(0);pads={{0,0,0x0200,0},{0,0,0x0200,0},{0,0,0x0002,0},{0,0,0,0},{0,0,0x84,0}};
 send_hid_to_core();assert((bytes==std::vector<unsigned>{2,0,0,0,0,2,0,0,0,0,0,0}));
 // Split chords from two controllers must not open the OSD.
 index=0;bytes.clear();overlay(0);pads={{4,128,0,0},{0x84,0,0,0}};send_hid_to_core();assert(index==1&&bytes.size()==4);
 // Software/keyboard overlay-on also exits gameplay input forwarding.
 index=0;bytes.clear();pads={{}};overlay(1);send_hid_to_core();assert(index==0);
 // The real forwarding loop recognizes OSD2 only on a capable core and P1.
 crt_stage1_capable=true;index=0;bytes.clear();overlay(0);
 pads={{0,0x84,0,0},{0,0,1,0},{0,0,0x144,0}};send_hid_to_core();
 assert(index==2&&bytes.size()==8&&game_osd_owner==Ui16GameOsd::Crt);
 index=0;bytes.clear();overlay(0);pads={{0x184,0,0,0}};send_hid_to_core();
 assert(bytes.empty()&&game_osd_owner==Ui16GameOsd::System);
 index=0;bytes.clear();overlay(0);pads={{0xc4,0,0,0},{0,0,0,0},{0x44,0,0,0}};
 send_hid_to_core();assert(index==2&&neutral_calls==1&&bytes.size()==4&&game_osd_owner==Ui16GameOsd::Crt);
 // SNES can implement ROM-preserving reset independently of CRT/OSD2.
 crt_stage1_capable=false;restart_capable=true;index=0;bytes.clear();overlay(0);
 pads={{0,0x84,0,0},{0x44,0,0,0},{0x84,0,0,0},{0,0,0,0},{0,0,0x184,0}};
 send_hid_to_core();assert(index==4&&game_osd_owner==Ui16GameOsd::System);
 // Ambiguous left+right must stay reserved through partial release.
 index=0;bytes.clear();overlay(0);pads={{0xc4,0,0,0},{0x84,0,0,0},{0,0,0,0},{0x84,0,0,0}};
 send_hid_to_core();assert(index==3&&game_osd_owner==Ui16GameOsd::System);
 restart_capable=false;
 const Ui16LaunchRequest seeded{1,"","dr.nes",{true,{true,1,2}}};
 crt_stage1_capable=true;
 for(auto error:{Ui16LaunchError::ProgramFailed,Ui16LaunchError::CoreTimeout,Ui16LaunchError::RomFailed,Ui16LaunchError::MissingRom}){
  launch_handoff(error,seeded);assert(session_calls==0&&releases==0);
 }
 crt_stage1_capable=false;launch_handoff(Ui16LaunchError::None,seeded);assert(session_calls==0&&releases==1);
 crt_stage1_capable=true;launch_handoff(Ui16LaunchError::None,seeded);
 assert(session_calls==1&&captured.present&&captured.settings==Ui16CrtSettings({true,1,2}));
 crt_rom_identity={true,Ui16RomVolume::Sd,1,0xabcdef01,65552};
 launch_handoff(Ui16LaunchError::None,Ui16LaunchRequest{1,"","other.nes"});assert(session_calls==2&&!captured.present);
 assert(captured_id.same_game(crt_rom_identity));
 launch_handoff(Ui16LaunchError::None,Ui16LaunchRequest{2,"","a.sfc",{true,{true,3,3}}});
 assert(preview_calls==1&&session_calls==2); // SNES ignores persisted/CSV/default state.
 snes_crt_readonly=true;crt_rom_identity.system=2;
 launch_handoff(Ui16LaunchError::None,Ui16LaunchRequest{2,"","a.sfc",{true,{true,2,1}}});
 assert(readonly_calls==1&&captured.present&&captured.settings==Ui16CrtSettings({true,2,1})&&captured_id.system==2);
 crt_rom_identity.system=1;
 active_core=2;core_running=true;
 const auto before_snes_save=save_calls;
 assert(!ui16_hw_save_crt(Ui16RomIdentity{true,Ui16RomVolume::Sd,2,123,456},{true,3,3})&&save_calls==before_snes_save);

 crt_defaults={Ui16CrtDefaultsState::Ready,Ui16CrtDefaultsSource::Global,{true,{true,1,1}}};
 launch_handoff(Ui16LaunchError::None,seeded);assert(captured_defaults.source==Ui16CrtDefaultsSource::Global&&captured_defaults.recommendation.present);
 crt_restore={Ui16CrtReadState::User,{true,{true,3,3}}};launch_handoff(Ui16LaunchError::None,seeded);assert(captured_restore.override_value.present);
 active_core=1;core_running=true;crt_stage1_capable=true;
 assert(ui16_hw_save_crt(crt_rom_identity,{true,1,2})&&save_calls==1&&!saved_reset);
 assert(ui16_hw_save_crt(crt_rom_identity,{true,1,2},true)&&save_calls==2&&saved_reset);
 active_core=6;assert(!ui16_hw_save_crt(crt_rom_identity,{})&&save_calls==2);
 active_core=1;core_running=false;assert(!ui16_hw_save_crt(crt_rom_identity,{})&&save_calls==2);
 core_running=true;crt_stage1_capable=false;assert(!ui16_hw_save_crt(crt_rom_identity,{})&&save_calls==2);
 assert(storage_depth==0);lease_available=false;
 assert(!ui16_hw_load_rom(2,"sd:snes/a.sfc",nullptr)&&!core_running&&!crt_rom_identity.valid&&!snes_crt_readonly);
 std::puts("Actual main.cpp adapters passed: stat/program/ID timeout/load state, display ownership, unmodified X/Y game HID, OSD chord, neutral packets");
}
'''
path=build/'launch_adapters_test.cpp';path.write_text(code,encoding='utf-8',newline='\n')
extra=[]
if os.name=='nt':
    converted=subprocess.check_output(['cygpath','-m','/ucrt64/bin'],text=True,encoding='utf-8').strip()
    extra=['-B'+converted+'/']
exe=build/('launch_adapters_test.exe' if os.name=='nt' else 'launch_adapters_test')
subprocess.run(['g++',*extra,'-DUI16_SYSTEM_MENU','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I'+str(ROOT/'ui'),'-I'+str(ROOT/'utils'),str(path),'-o',str(exe)],check=True)
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
#include "ui16_osd_menu.h"
#include "ui16_system_osd.h"
#include "ui16_crt.h"
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
bool ui16_hw_starting(void *);
void overlay_osd_window(unsigned,unsigned,unsigned,unsigned);
extern bool crt_stage1_capable,restart_capable,core_running;
extern bool snes_crt_readonly;
extern int active_core;
void fpga_tx_header(int,int);
#define DEBUG(...) ((void)0)
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
extern Ui16RomIdentity crt_rom_identity;
extern Ui16CrtRestore crt_restore;
extern Ui16CrtDefaults crt_defaults;
bool ui16_hw_save_crt(const Ui16RomIdentity &,const Ui16CrtSettings &,bool);
static bool crt_blur_capable=false,crt_scanline_capable=false;
extern Ui16GameOsd game_osd_owner;
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
