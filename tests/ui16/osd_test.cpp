#include "ui16_osd.h"
#include "ui16_osd_menu.h"
#include "ui16_system_osd.h"
#include "menu_manager.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <algorithm>
#include <vector>

static std::uint64_t now=0;
struct Pad{std::uint16_t a=0;};static std::vector<Pad> pads;static unsigned at=0;
static std::vector<std::array<unsigned,4>> boxes;
static std::array<char,32*28> hardware;
static std::vector<std::array<char,32*28>> snapshots;
static unsigned cursor_x=0,cursor_y=0,writes=0,clears=0;
int _overlay_on=1;
void get_joypad_states(std::uint16_t *a,std::uint16_t *b,std::uint16_t *c,std::uint16_t *d){assert(at<pads.size());snapshots.push_back(hardware);*a=pads[at++].a;*b=*c=*d=0;}
std::uint64_t bflb_mtimer_get_time_ms(){return now;}
void delay(unsigned n){now+=n;}
int overlay_on(){return _overlay_on;}
void overlay(int v){_overlay_on=v;}
void overlay_osd_capture_begin(){ui16_osd_capture_begin();}
void overlay_osd_capture_end(){ui16_osd_capture_end();}
void overlay_osd_window(unsigned x,unsigned y,unsigned w,unsigned h){boxes.push_back({x,y,w,h});}
void overlay_clear(){if(!ui16_osd_capture_clear()){hardware.fill(' ');++clears;}}
void overlay_cursor(int x,int y){if(!ui16_osd_capture_cursor(x,y)){cursor_x=x;cursor_y=y;}}
void overlay_printf(const char *fmt,...){
    char buf[256];va_list a;va_start(a,fmt);std::vsnprintf(buf,sizeof(buf),fmt,a);va_end(a);
    if(ui16_osd_capture_text(buf))return;
    for(const char *p=buf;*p;++p){assert(cursor_x<32&&cursor_y<28);hardware[cursor_y*32+cursor_x++]=*p;++writes;}
}
void dprint(const char *,...){}
std::uint8_t key_input(){return 0;}
void command_console(){}
static int adjustments=0;
struct FutureMenu:Menu{
    int value=0;
    void render()override{overlay_clear();overlay_cursor(0,10);overlay_printf("FILTER %d",value);overlay_cursor(0,11);overlay_printf("MAIN MENU");}
    std::vector<int> get_options()override{return {10,11};}
    bool on_adjust(int i,int delta)override{if(i!=0)return false;value+=delta;++adjustments;return true;}
    bool on_choose(int i)override{return i==1;}
    int osd_width()const override{return 20;}
};
struct DialogMenu:FutureMenu{
    bool on_choose(int i)override{if(i==0)hardware.fill('!');return false;}
};
struct InlineMenu:FutureMenu{
    bool inline_choice(int i)const override{return i==0;}
    bool on_choose(int i)override{if(i==0){on_adjust(0,1);redraw=true;}return false;}
};
struct VariableMenu:FutureMenu{
    void render()override{
        overlay_clear();overlay_cursor(0,10);overlay_printf("FILTER %s",value?"OFF":"STANDARD");
        overlay_cursor(0,11);overlay_printf("MAIN MENU");
    }
};
static void reset_output(){boxes.clear();snapshots.clear();hardware.fill('?');writes=clears=0;at=0;now=0;_overlay_on=1;}
static void check_screen(const std::array<char,32*28> &actual,const Ui16OsdFrame &frame){
    for(unsigned y=frame.y;y<frame.y+frame.height;++y)
        for(unsigned x=frame.x;x<frame.x+frame.width;++x)assert(actual[y*32+x]==frame.cells[y*32+x]);
}
int main(){
    ui16_osd_capture_begin();overlay_clear();overlay_cursor(0,13);overlay_printf("<< Main Menu");ui16_osd_capture_end();
    int top=0,width=0;
    auto first=ui16_osd_layout(ui16_osd_canvas(),{13},0,top,width,Ui16OsdPosition::TopRight);
    assert(first.height==6&&first.width==19&&first.x==12&&first.y==1);
    for(int p=0;p<5;++p){auto f=ui16_osd_layout(ui16_osd_canvas(),{13},1,top,width,static_cast<Ui16OsdPosition>(p));
        assert(f.width==19&&f.height==6&&f.x+f.width<=31&&f.y+f.height<=27);
        assert(f.cells[f.y*32+f.x]=='+');}
    // Width remains stable across value changes; large content scrolls within the display.
    ui16_osd_capture_begin();for(int i=0;i<70;++i){overlay_cursor(0,i);overlay_printf("PARAM %d",i);}ui16_osd_capture_end();
    std::vector<int> opts;for(int i=0;i<70;++i)opts.push_back(i);
    top=0;width=25;auto many=ui16_osd_layout(ui16_osd_canvas(),opts,69,top,width,Ui16OsdPosition::BottomRight);
    assert(many.height==26&&many.width==28&&top==49&&many.y==1);
    auto narrow=ui16_osd_layout(ui16_osd_canvas(),opts,0,top,width,Ui16OsdPosition::TopRight);assert(narrow.width==28&&top==0);
    // Actual popup loop: select POS, change right/left, redraw immediately, preserve selection.
    menu_clear();push_menu(std::make_unique<DefaultMenu>());_overlay_on=1;at=0;now=0;boxes.clear();
    pads={{0x20},{0},{0x80},{0},{0x40},{0},{0x200}};
    ui16_osd_menu_input_loop();assert(!overlay_on());
    std::vector<std::array<unsigned,4>> shown;for(auto b:boxes)if(b[2])shown.push_back(b);
    assert(shown.size()==3&&shown[0][0]==12&&shown[0][1]==1);
    assert(shown[1][0]==1&&shown[1][1]==21); // TOP RIGHT -> BOTTOM LEFT
    assert(shown[2][0]==12&&shown[2][1]==1);
    for(auto b:boxes)assert(b[2]&&b[3]); // Never hide the window while updating.
    // Reopening retains the chosen position; A/B on the POS row does not exit.
    menu_clear();auto ptr=std::make_unique<FutureMenu>();push_menu(std::move(ptr));
    _overlay_on=1;at=0;boxes.clear();pads={{0x80},{0},{0x20},{0},{1}};
    ui16_osd_menu_input_loop();assert(overlay_on()&&!menu_is_active()); // Main Menu selected.
    assert(adjustments==1);
    // Moving selection changes two markers only, without clearing text RAM or
    // sending another window command. Replay actual writes to verify the result.
    menu_clear();push_menu(std::make_unique<FutureMenu>());reset_output();
    pads={{0x20},{0},{0x10},{0},{0x200}};ui16_osd_menu_input_loop();
    top=0;width=20;
    const auto selected0=ui16_osd_layout(ui16_osd_canvas(),{10,11},0,top,width,Ui16OsdPosition::TopRight);
    const auto selected1=ui16_osd_layout(ui16_osd_canvas(),{10,11},1,top,width,Ui16OsdPosition::TopRight);
    assert(boxes.size()==1&&clears==0&&writes==selected0.width*selected0.height+4);
    check_screen(snapshots[0],selected0);check_screen(snapshots[1],selected1);check_screen(snapshots[3],selected0);
    // Changing a value does not erase static labels or the panel border.
    menu_clear();push_menu(std::make_unique<FutureMenu>());reset_output();
    pads={{0x80},{0},{0x40},{0},{0x200}};ui16_osd_menu_input_loop();
    assert(boxes.size()==1&&clears==0&&writes==selected0.width*selected0.height+2);
    check_screen(snapshots[0],selected0);check_screen(snapshots[4],selected0);
    // Shorter labels must erase their former suffix, without a panel-wide clear.
    menu_clear();push_menu(std::make_unique<VariableMenu>());reset_output();
    pads={{0x80},{0},{0x200}};ui16_osd_menu_input_loop();
    top=0;width=20;
    const auto shorter=ui16_osd_layout(ui16_osd_canvas(),{10,11},0,top,width,Ui16OsdPosition::TopRight);
    assert(boxes.size()==1&&clears==0);check_screen(snapshots[1],shorter);
    // A dialog can bypass the renderer and overwrite RAM. Returning must
    // invalidate the cached frame and restore even unchanged characters.
    menu_clear();push_menu(std::make_unique<DialogMenu>());reset_output();
    pads={{1},{0},{0},{0x200}};ui16_osd_menu_input_loop();
    assert(clears==0&&writes==2*selected0.width*selected0.height);
    check_screen(snapshots[2],selected0);
    for(auto b:boxes)assert(b[2]&&b[3]);
    // A held confirm button must not block in-place rendering or tick polling.
    menu_clear();push_menu(std::make_unique<InlineMenu>());reset_output();
    pads={{1},{1},{1},{0},{0x200}};ui16_osd_menu_input_loop();
    top=0;width=20;
    const auto inline_frame=ui16_osd_layout(ui16_osd_canvas(),{10,11},0,top,width,Ui16OsdPosition::TopRight);
    assert(boxes.size()==1&&clears==0);check_screen(snapshots[1],inline_frame);
    check_screen(snapshots[2],inline_frame);
    // Real OSD1 menu and input loop: cancel cannot request either action.
    Ui16SystemAction action;
    menu_clear();push_menu(std::unique_ptr<Menu>(ui16_create_system_osd(action,true)));reset_output();
    pads={{0x44},{1},{1},{0},{0x200}};
    ui16_osd_menu_input_loop(true,"SYSTEM",true);
    assert(action==Ui16SystemAction::None&&!overlay_on());
    assert(boxes.size()==1&&boxes[0][0]==(32-boxes[0][2])/2&&boxes[0][1]==(28-boxes[0][3])/2);
    // Opposite chord and its held suffix cannot confirm Restart Game.
    menu_clear();push_menu(std::unique_ptr<Menu>(ui16_create_system_osd(action,true)));reset_output();
    pads={{0x45},{1},{0},{0x20},{0},{1},{0}};
    ui16_osd_menu_input_loop(true,"SYSTEM",true);
    assert(action==Ui16SystemAction::ReturnToList&&overlay_on()&&!menu_is_active());
    // Restart only becomes a request after a fresh confirmation. The main-task
    // dispatcher owns the final release wait before transmitting reset.
    menu_clear();push_menu(std::unique_ptr<Menu>(ui16_create_system_osd(action,true)));reset_output();
    pads={{1},{1},{1},{0}};
    ui16_osd_menu_input_loop(true,"SYSTEM",true);
    assert(action==Ui16SystemAction::Restart&&at==1);
    // Old images retain the list return path but never claim reset support.
    menu_clear();push_menu(std::unique_ptr<Menu>(ui16_create_system_osd(action,false)));reset_output();
    pads={{1},{0},{0},{0x20},{0},{1},{0}};
    ui16_osd_menu_input_loop(true,"SYSTEM",true);
    assert(action==Ui16SystemAction::ReturnToList&&overlay_on());
    menu_clear();std::puts("OSD host tests passed: OSD1 actions, fixed center, old-image guard, chord suffix/held-confirm safety; five OSD2 positions and in-place rendering");
}
