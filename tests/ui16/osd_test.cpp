#include "ui16_osd.h"
#include "ui16_osd_menu.h"
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
int _overlay_on=1;
void get_joypad_states(std::uint16_t *a,std::uint16_t *b,std::uint16_t *c,std::uint16_t *d){assert(at<pads.size());*a=pads[at++].a;*b=*c=*d=0;}
std::uint64_t bflb_mtimer_get_time_ms(){return now;}
void delay(unsigned n){now+=n;}
int overlay_on(){return _overlay_on;}
void overlay(int v){_overlay_on=v;}
void overlay_osd_capture_begin(){ui16_osd_capture_begin();}
void overlay_osd_capture_end(){ui16_osd_capture_end();}
void overlay_osd_window(unsigned x,unsigned y,unsigned w,unsigned h){boxes.push_back({x,y,w,h});}
void overlay_clear(){ui16_osd_capture_clear();}
void overlay_cursor(int x,int y){ui16_osd_capture_cursor(x,y);}
void overlay_printf(const char *fmt,...){char buf[256];va_list a;va_start(a,fmt);std::vsnprintf(buf,sizeof(buf),fmt,a);va_end(a);ui16_osd_capture_text(buf);}
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
    assert(shown.size()>=4&&shown[0][0]==12&&shown[0][1]==1);
    assert(shown[2][0]==1&&shown[2][1]==21); // TOP RIGHT -> BOTTOM LEFT
    assert(shown[3][0]==12&&shown[3][1]==1);
    // Reopening retains the chosen position; A/B on the POS row does not exit.
    menu_clear();auto ptr=std::make_unique<FutureMenu>();push_menu(std::move(ptr));
    _overlay_on=1;at=0;boxes.clear();pads={{0x80},{0},{0x20},{0},{1}};
    ui16_osd_menu_input_loop();assert(overlay_on()&&!menu_is_active()); // Main Menu selected.
    assert(adjustments==1);
    menu_clear();std::puts("OSD host tests passed: dynamic dimensions, five anchors, stable width, scrolling, immediate move, selection, future parameter adjustment, Main Menu/resume");
}
