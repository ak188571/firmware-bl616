#ifdef UI16_SYSTEM_MENU
#include "ui16_osd_menu.h"
#include "ui16_osd.h"
#include "ui16_navigation.h"
#include "ui16_browser.h"
#include "menu_manager.h"
#include "utils.h"
#include <algorithm>

namespace { Ui16OsdPosition position=Ui16OsdPosition::TopRight; }
void ui16_osd_menu_input_loop()
{
    if(!menu_is_active())return;
    Menu *shown=nullptr;
    std::vector<int> options;
    Ui16Navigation nav(0);
    int top=0,width_floor=0;
    std::uint16_t previous=0;
    bool dirty=true;
    while(menu_is_active()&&overlay_on()){
        auto *menu=menu_current();
        if(shown!=menu){
            shown=menu;options=menu->get_options();nav=Ui16Navigation(options.size()+1);
            top=0;width_floor=menu->osd_width();dirty=true;
        }
        if(dirty||menu->redraw){
            const auto updated=menu->get_options();
            if(updated!=options){
                const int selected=nav.selected();options=updated;
                nav=Ui16Navigation(options.size()+1,selected);
            }
            overlay_osd_capture_begin();menu->render();overlay_osd_capture_end();
            const auto frame=ui16_osd_layout(ui16_osd_canvas(),options,nav.selected(),top,width_floor,position);
            // Hide old geometry while replacing character cells; input remains blocked.
            overlay_osd_window(0,0,0,0);
            overlay_clear();
            for(unsigned row=frame.y;row<frame.y+frame.height;++row){
                overlay_cursor(frame.x,row);
                for(unsigned x=frame.x;x<frame.x+frame.width;++x)overlay_printf("%c",frame.cells[row*32+x]);
            }
            overlay_osd_window(frame.x,frame.y,frame.width,frame.height);
            menu->redraw=false;dirty=false;
        }
        std::uint16_t j1=0,j2=0,h1=0,h2=0;
        get_joypad_states(&j1,&j2,&h1,&h2);
        const auto buttons=static_cast<std::uint16_t>(j1|j2|h1|h2);
        const auto pressed=static_cast<std::uint16_t>(buttons&~previous);previous=buttons;
        const bool conflict=(buttons&UI16_BUTTON_CONFIRM)&&(buttons&UI16_BUTTON_BACK);
        if((j1==OSD_KEY_CODE||j2==OSD_KEY_CODE||h1==OSD_KEY_CODE||h2==OSD_KEY_CODE)&&pressed){overlay(0);return;}
        if(!conflict&&(pressed&UI16_BUTTON_BACK)){overlay(0);return;}
        const auto now=static_cast<std::uint32_t>(bflb_mtimer_get_time_ms());
        if(nav.update(buttons,now))dirty=true;
        const bool left=(pressed&0x40)!=0,right=(pressed&0x80)!=0;
        if(left!=right){
            const int delta=right?1:-1;
            if(nav.selected()==static_cast<int>(options.size())){
                position=static_cast<Ui16OsdPosition>((static_cast<int>(position)+delta+5)%5);dirty=true;
            }else if(menu->on_adjust(nav.selected(),delta))dirty=true;
        }
        if(!conflict&&(pressed&UI16_BUTTON_CONFIRM)&&nav.selected()<static_cast<int>(options.size())){
            const bool full=menu->fullscreen_dialog(nav.selected());
            if(full)overlay_osd_window(0,0,32,28);
            const bool close=menu->on_choose(nav.selected());
            if(close&&menu_is_active()&&menu_current()==menu)pop_menu();
            if(!menu_is_active())return;
            // Do not carry a button held in a child dialog to this popup.
            do{get_joypad_states(&j1,&j2,&h1,&h2);if(j1|j2|h1|h2)delay(10);}while(j1|j2|h1|h2);
            previous=0;dirty=true;
        }
        delay(10);
    }
}
#endif
