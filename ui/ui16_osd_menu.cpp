#ifdef UI16_SYSTEM_MENU
#include "ui16_osd_menu.h"
#include "ui16_osd.h"
#include "ui16_crt.h"
#include "ui16_navigation.h"
#include "ui16_browser.h"
#include "menu_manager.h"
#include "utils.h"
#include <algorithm>

namespace { Ui16OsdPosition position=Ui16OsdPosition::TopRight; }
void ui16_osd_menu_input_loop(bool fixed_center,const char *title,bool dual_osd)
{
    if(!menu_is_active())return;
    Menu *shown=nullptr;
    std::vector<int> options;
    Ui16Navigation nav(0);
    int top=0,width_floor=0;
    std::uint16_t previous=0;
    bool dirty=true,chord_release=false;
    // The UI has one owning task, as does the existing capture canvas. Keep the
    // saved frame out of that task's call stack; validity is reset on each entry.
    static Ui16OsdFrame displayed;
    bool displayed_valid=false;
    while(menu_is_active()&&overlay_on()){
        auto *menu=menu_current();
        if(shown!=menu){
            shown=menu;options=menu->get_options();nav=Ui16Navigation(options.size()+(fixed_center?0:1));
            top=0;width_floor=menu->osd_width();dirty=true;displayed_valid=false;
        }
        if(menu->on_tick(static_cast<std::uint32_t>(bflb_mtimer_get_time_ms())))dirty=true;
        if(dirty||menu->redraw){
            const auto updated=menu->get_options();
            if(updated!=options){
                const int selected=nav.selected();options=updated;
                nav=Ui16Navigation(options.size()+(fixed_center?0:1),selected);
            }
            overlay_osd_capture_begin();menu->render();overlay_osd_capture_end();
            const auto frame=ui16_osd_layout(ui16_osd_canvas(),options,nav.selected(),top,width_floor,fixed_center?Ui16OsdPosition::Center:position,!fixed_center,title,menu->osd_style());
            const bool same_geometry=displayed_valid && frame.x==displayed.x && frame.y==displayed.y &&
                frame.width==displayed.width && frame.height==displayed.height;
            // Keep the current window visible. Send only changed character runs
            // for selection/value updates; a move is populated before committing
            // its new geometry at the FPGA's next frame boundary.
            for(unsigned row=frame.y;row<frame.y+frame.height;++row){
                unsigned x=frame.x;
                while(x<frame.x+frame.width){
                    if(same_geometry && frame.cells[row*32+x]==displayed.cells[row*32+x]){++x;continue;}
                    const unsigned start=x;
                    char run[33];unsigned length=0;
                    do{run[length++]=frame.cells[row*32+x++];}
                    while(x<frame.x+frame.width && (!same_geometry || frame.cells[row*32+x]!=displayed.cells[row*32+x]));
                    run[length]='\0';
                    overlay_cursor(start,row);overlay_printf("%s",run);
                }
            }
            if(!same_geometry)overlay_osd_window(frame.x,frame.y,frame.width,frame.height);
            displayed=frame;displayed_valid=true;
            menu->redraw=false;dirty=false;
        }
        std::uint16_t j1=0,j2=0,h1=0,h2=0;
        get_joypad_states(&j1,&j2,&h1,&h2);
        const auto buttons=static_cast<std::uint16_t>(dual_osd?(j1|h1):(j1|j2|h1|h2));
        // Other OSD chords are ignored while a popup owns input. Wait for all
        // buttons to release so their directions cannot adjust the current OSD.
        if(dual_osd && (ui16_osd_reserved(j1)||ui16_osd_reserved(h1)))chord_release=true;
        if(chord_release){
            previous=buttons;
            if((j1|j2|h1|h2)==0){chord_release=false;previous=0;}
            delay(10);continue;
        }
        const auto pressed=static_cast<std::uint16_t>(buttons&~previous);previous=buttons;
        const bool conflict=(buttons&UI16_BUTTON_CONFIRM)&&(buttons&UI16_BUTTON_BACK);
        if(!dual_osd&&(j1==OSD_KEY_CODE||j2==OSD_KEY_CODE||h1==OSD_KEY_CODE||h2==OSD_KEY_CODE)&&pressed){overlay(0);return;}
        if(!conflict&&(pressed&UI16_BUTTON_BACK)){
            if(menu->on_close()){overlay(0);return;}
            dirty=true;delay(10);continue;
        }
        // START only changes the information view. A held button must not
        // toggle repeatedly or perform an edit/SAVE on the same input edge.
        if((pressed&0x0008)&&menu->on_info(buttons)){
            if(buttons!=0x0008)chord_release=true;
            top=0;width_floor=menu->osd_width();dirty=true;
            delay(10);continue;
        }
        const auto now=static_cast<std::uint32_t>(bflb_mtimer_get_time_ms());
        if(nav.update(buttons,now))dirty=true;
        const bool left=(pressed&0x40)!=0,right=(pressed&0x80)!=0;
        if(left!=right){
            const int delta=right?1:-1;
            if(!fixed_center && nav.selected()==static_cast<int>(options.size())){
                position=static_cast<Ui16OsdPosition>((static_cast<int>(position)+delta+5)%5);dirty=true;
            }else if(menu->on_adjust(nav.selected(),delta))dirty=true;
        }
        if(!conflict&&(pressed&UI16_BUTTON_CONFIRM)&&nav.selected()<static_cast<int>(options.size())){
            const bool inline_change=menu->inline_choice(nav.selected());
            const bool full=menu->fullscreen_dialog(nav.selected());
            if(full)overlay_osd_window(0,0,32,28);
            const bool close=menu->on_choose(nav.selected());
            // A dialog or another direct writer may have changed character RAM.
            if(!inline_change)displayed_valid=false;
            if(close&&menu_is_active()&&menu_current()==menu)pop_menu();
            if(!menu_is_active())return;
            // Do not carry a button held in a child dialog to this popup.
            if(!inline_change){
                do{get_joypad_states(&j1,&j2,&h1,&h2);if(j1|j2|h1|h2)delay(10);}while(j1|j2|h1|h2);
                previous=0;
            }
            dirty=true;
        }
        delay(10);
    }
}
#endif
