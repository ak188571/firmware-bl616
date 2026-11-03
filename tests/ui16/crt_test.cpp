#include "ui16_crt.h"
#include "ui16_osd.h"
#include "ui16_osd_menu.h"
#include "menu_manager.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <memory>
#include <string>

static std::uint32_t config=0xa5e01234;
static std::uint64_t now=0;
static std::vector<std::uint16_t> pads;
static unsigned at=0;
static std::vector<std::array<unsigned,4>> boxes;
int _overlay_on=1,critical_depth=0;
void enter_critical(){++critical_depth;}void leave_critical(){--critical_depth;}
void fpga_tx_header(int c,int n){assert(c==0x12&&n==1);}
void fpga_tx_byte(std::uint8_t){}
std::uint32_t get_core_config(){return config;}
void set_core_config(std::uint32_t v){config=v;}
std::uint64_t bflb_mtimer_get_time_ms(){return now;}
void delay(unsigned ms){now+=ms;}
void get_joypad_states(std::uint16_t *a,std::uint16_t *b,std::uint16_t *c,std::uint16_t *d){
    assert(at<pads.size());*a=pads[at++];*b=*c=*d=0;
}
int overlay_on(){return _overlay_on;}void overlay(int v){_overlay_on=v;}
void overlay_osd_capture_begin(){ui16_osd_capture_begin();}
void overlay_osd_capture_end(){ui16_osd_capture_end();}
void overlay_osd_window(unsigned x,unsigned y,unsigned w,unsigned h){boxes.push_back({x,y,w,h});}
void overlay_clear(){ui16_osd_capture_clear();}
void overlay_cursor(int x,int y){ui16_osd_capture_cursor(x,y);}
void overlay_printf(const char *fmt,...){char s[256];va_list a;va_start(a,fmt);std::vsnprintf(s,sizeof s,fmt,a);va_end(a);ui16_osd_capture_text(s);}
void dprint(const char *,...){}std::uint8_t key_input(){return 0;}void command_console(){}

int main(){
    assert(ui16_osd_request(0x84,0)==Ui16GameOsd::System);
    assert(ui16_osd_request(0,0x44)==Ui16GameOsd::Crt);
    assert(ui16_osd_request(0x184,0)==Ui16GameOsd::System); // extra A
    assert(ui16_osd_request(4,0x40)==Ui16GameOsd::None); // no cross-source chord
    assert(ui16_osd_request(0xc4,0)==Ui16GameOsd::None);
    assert(ui16_osd_request(0x84,0x44)==Ui16GameOsd::None);
    assert(ui16_osd_reserved(0xc4)&&!ui16_osd_reserved(0x40));
    const auto preserved=config&~UI16_CRT_MASK;
    ui16_crt_new_session();assert(config==preserved);
    std::unique_ptr<Menu> menu(ui16_create_crt_menu());
    assert(menu->on_adjust(0,1));assert(config==(preserved|0x10000));
    assert(menu->on_adjust(1,-1));assert(config==(preserved|0x70000));
    assert(menu->on_adjust(2,1));assert(config==(preserved|0xf0000));
    assert(!menu->on_adjust(3,1));
    ui16_crt_accept_status(config);assert(menu->on_tick(10));
    ui16_osd_capture_begin();menu->render();ui16_osd_capture_end();
    const auto &cells=ui16_osd_canvas().cells;
    assert(std::string(cells.data()+7*32,16).find("FRAME ACK")!=std::string::npos);
    // Opening the opposite OSD must neither switch ownership nor adjust a value.
    menu_clear();push_menu(std::move(menu));_overlay_on=1;at=0;boxes.clear();
    pads={0x84,0,0x200};
    ui16_osd_menu_input_loop(false,"CRT SETTINGS",true);
    assert(!overlay_on()&&config==(preserved|0xf0000));
    menu_clear();push_menu(std::make_unique<DefaultMenu>());_overlay_on=1;at=0;boxes.clear();
    pads={0x20,0,0x80,0,0x200};
    ui16_osd_menu_input_loop(true,"SYSTEM",true);
    for(auto b:boxes)if(b[2]){assert(b[0]==(32-b[2])/2&&b[1]==(28-b[3])/2);}
    assert(!overlay_on());menu_clear();
    // New game clears only this stage's reserved bits; the session is not saved.
    ui16_crt_new_session();assert(config==preserved&&critical_depth==0);
    // The effect-capable core advertises actual blur and omits scanline control.
    std::unique_ptr<Menu> blur_menu(ui16_create_crt_menu(true));
    assert((blur_menu->get_options()==std::vector<int>{3,4,9}));
    assert(blur_menu->on_adjust(0,1));assert(config==(preserved|0x10000));
    assert(blur_menu->on_adjust(1,1));assert(config==(preserved|0x30000));
    assert(!blur_menu->on_adjust(2,1));assert(config==(preserved|0x30000));
    ui16_crt_accept_status(config);assert(blur_menu->on_tick(20));
    ui16_osd_capture_begin();blur_menu->render();ui16_osd_capture_end();
    const auto &blur_cells=ui16_osd_canvas().cells;
    assert(std::string(blur_cells.data(),32).find("H BLUR READY")!=std::string::npos);
    assert(std::string(blur_cells.data()+5*32,32).find("NOT YET")!=std::string::npos);
    assert(std::string(blur_cells.data()+7*32,32).find("FRAME ACK")!=std::string::npos);
    assert(blur_menu->on_choose(2)&&!overlay_on());
    ui16_crt_new_session();assert(config==preserved);
    std::unique_ptr<Menu> scan_menu(ui16_create_crt_menu(true,true));
    assert((scan_menu->get_options()==std::vector<int>{3,4,5,9}));
    assert(scan_menu->on_adjust(0,1));assert(scan_menu->on_adjust(1,1));
    for(unsigned level=1;level<=4;level++){
        assert(scan_menu->on_adjust(2,1));
        assert(config==(preserved|0x30000|((level%4)<<19)));
    }
    assert(scan_menu->on_adjust(2,-1));assert(config==(preserved|0x1b0000));
    ui16_crt_accept_status(config);assert(scan_menu->on_tick(30));
    ui16_osd_capture_begin();scan_menu->render();ui16_osd_capture_end();
    const auto &scan_cells=ui16_osd_canvas().cells;
    assert(std::string(scan_cells.data(),32).find("CRT READY - STAGE4B")!=std::string::npos);
    assert(std::string(scan_cells.data()+5*32,32).find("STRONG")!=std::string::npos);
    assert(std::string(scan_cells.data()+7*32,32).find("FRAME ACK")!=std::string::npos);
    assert(scan_menu->on_choose(3)&&!overlay_on());
    ui16_crt_new_session();assert(config==preserved);
    std::puts("CRT host PASS: independent hotkeys, conflict reservation, preserved config bits, frame acknowledgement, exclusive popup, fixed OSD1, session reset");
}
