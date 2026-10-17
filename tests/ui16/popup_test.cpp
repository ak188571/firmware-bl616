#include "menu_manager.h"
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
struct Pad {std::uint16_t one=0,two=0,hid1=0,hid2=0;};
static std::vector<Pad> pads;static unsigned at=0;int _overlay_on=1;
void get_joypad_states(std::uint16_t *a,std::uint16_t *b,std::uint16_t *c,std::uint16_t *d){assert(at<pads.size());auto p=pads[at++];*a=p.one;*b=p.two;*c=p.hid1;*d=p.hid2;}
void delay(unsigned){}
int overlay_on(){return _overlay_on;}
void overlay(int on){_overlay_on=on;}
void overlay_clear(){}
void overlay_cursor(int,int){}
void overlay_printf(const char *,...){}
std::uint8_t key_input(){return 0;}
void command_console(){}
struct TestMenu:Menu {
    int &selected;
    explicit TestMenu(int &s):selected(s){}
    void render() override{}
    std::vector<int> get_options() override{return {10,12};}
    bool on_choose(int i) override{selected=i;return true;}
};
int main(){
    for(unsigned button:{0x0100u,0x0001u}){
        int selected=-1;menu_clear();push_menu(std::make_unique<TestMenu>(selected));
        overlay(1);at=0;pads={{0x20},{0},{static_cast<std::uint16_t>(button)}};
        menu_input_loop();assert(selected==1&&overlay_on()&&!menu_is_active());
    }
    for(unsigned button:{0x0200u,0x0002u,0x84u}){
        int selected=-1;menu_clear();push_menu(std::make_unique<TestMenu>(selected));
        overlay(1);at=0;pads={{0,0,static_cast<std::uint16_t>(button)}};
        menu_input_loop();assert(selected==-1&&!overlay_on());
    }
    menu_clear();std::puts("Actual popup loop passed: A/B Main Menu, X/Y resume, OSD close, HID source");
}
