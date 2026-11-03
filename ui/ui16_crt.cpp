#ifdef UI16_SYSTEM_MENU
#include "ui16_crt.h"
#include "ui16_crt_store.h"
#include "menu_manager.h"
#include "utils.h"

namespace {
Ui16CrtSettings settings;
Ui16CrtStore store;
bool prepared=false,dirty=false,save_failed=false;
volatile std::uint32_t applied_status=0;
volatile bool status_seen=false;
std::uint32_t last_send_ms=0;
void send_settings() {
    last_send_ms=static_cast<std::uint32_t>(bflb_mtimer_get_time_ms());
    taskENTER_CRITICAL();status_seen=false;taskEXIT_CRITICAL();
    set_core_config(settings.merge(get_core_config()));
}
const char *strength(unsigned n) {
    static const char *const names[]={"OFF","WEAK","STANDARD","STRONG"};
    return names[n&3u];
}
struct CrtMenu:Menu {
    explicit CrtMenu(bool blur,bool scan):blur_supported(blur),scan_supported(blur&&scan){}
    const bool blur_supported,scan_supported;
    std::uint32_t last_poll=0;
    bool matched=false;
    int parameters()const{return blur_supported&&!scan_supported?2:3;}
    int close_index()const{return parameters()+(prepared?1:0);}
    int osd_width()const override{return 23;}
    void render()override {
        overlay_clear();overlay_cursor(0,0);overlay_printf("%s",prepared&&scan_supported?"CRT READY - STAGE5":scan_supported?"CRT READY - STAGE4B":blur_supported?"H BLUR READY - STAGE4A":"BYPASS ONLY - STAGE 1");
        overlay_cursor(0,1);overlay_printf("%s",prepared?"CLOSE SAVES CHANGES":"SESSION ONLY / NO SAVE");
        overlay_cursor(0,3);overlay_printf("%s %s",blur_supported?"CRT ENABLE  ":"CRT REQUEST ",settings.enabled?"ON":"OFF");
        overlay_cursor(0,4);overlay_printf("%s %s",blur_supported?"H BLUR      ":"H BLUR REQ  ",strength(settings.blur));
        overlay_cursor(0,5);if(scan_supported)overlay_printf("SCANLINE     %s",strength(settings.scanline));
        else if(blur_supported)overlay_printf("SCANLINE     NOT YET");
        else overlay_printf("SCANLINE REQ %s",strength(settings.scanline));
        overlay_cursor(0,7);overlay_printf("APPLY: %s",matched?"FRAME ACK":"WAITING");
        if(prepared){
            overlay_cursor(0,8);overlay_printf("SAVE: %s",save_failed?"FAILED - CLOSE RETRY":!store.ready?"NO ROM ID":dirty?"UNSAVED":store.user_override?"SAVED":"RECOMMENDED");
            overlay_cursor(0,9);overlay_printf("SOURCE: %s",(!dirty&&store.user_override)||settings!=store.recommended?"USER":store.recommended_source);
            overlay_cursor(0,10);if(store.notice[0]!='O'||store.notice[1]!='K')overlay_printf("NOTE: %s",store.notice);
            overlay_cursor(0,11);overlay_printf("RESET TO RECOMMENDED");
            overlay_cursor(0,13);overlay_printf("CLOSE");
            if(save_failed){overlay_cursor(0,14);overlay_printf("LEAVE UNSAVED");}
        }else{overlay_cursor(0,9);overlay_printf("CLOSE");}
    }
    std::vector<int> get_options()override{
        std::vector<int> rows={3,4};if(parameters()==3)rows.push_back(5);
        if(prepared){rows.push_back(11);rows.push_back(13);if(save_failed)rows.push_back(14);}else rows.push_back(9);
        return rows;
    }
    bool on_adjust(int row,int delta)override {
        if(row==0)settings.enabled=!settings.enabled;
        else if(row==1)settings.blur=(settings.blur+(delta>0?1:3))%4;
        else if(row==2&&(!blur_supported||scan_supported))settings.scanline=(settings.scanline+(delta>0?1:3))%4;
        else return false;
        if(prepared){dirty=store.dirty(settings);save_failed=false;}
        matched=false;send_settings();return true;
    }
    bool on_choose(int row)override {
        if(prepared&&row==parameters()){
            settings=store.recommended;dirty=store.dirty(settings);save_failed=false;matched=false;
            send_settings();redraw=true;return false;
        }
        if(row==close_index()){
            if(!on_close()){redraw=true;return false;}overlay(0);return true;
        }
        if(prepared&&save_failed&&row==close_index()+1){overlay(0);return true;}
        on_adjust(row,1);redraw=true;return false;
    }
    bool inline_choice(int row)const override{return row<close_index();}
    bool on_close()override{
        if(prepared&&dirty){
            if(!store.save(settings)){save_failed=true;redraw=true;return false;}
            dirty=false;save_failed=false;
        }
        return true;
    }
    bool on_tick(std::uint32_t now)override {
        bool seen;std::uint32_t value;
        taskENTER_CRITICAL();seen=status_seen;value=applied_status;taskEXIT_CRITICAL();
        const bool next=seen&&((value^settings.merge(0))&UI16_CRT_MASK)==0;
        const bool changed=next!=matched;matched=next;
        // Recover a lost setting packet instead of waiting indefinitely for a
        // value that was never received. Requests are idempotent and coalesce.
        if(!matched && static_cast<std::uint32_t>(now-last_send_ms)>=250)send_settings();
        if(static_cast<std::uint32_t>(now-last_poll)>=100){
            taskENTER_CRITICAL();fpga_tx_header(0x12,1);taskEXIT_CRITICAL();last_poll=now;
        }
        return changed;
    }
};
}
void ui16_crt_prepare_game(int core_id,const char *rom_path){
    store.prepare(core_id,rom_path,[](){delay(1);});settings=store.initial;prepared=true;dirty=false;save_failed=false;
}
void ui16_crt_end_session(){prepared=false;dirty=false;save_failed=false;store=Ui16CrtStore{};settings={};}
void ui16_crt_new_session(){if(!prepared)settings={};send_settings();}
void ui16_crt_accept_status(std::uint32_t applied) {
    taskENTER_CRITICAL();applied_status=applied;status_seen=true;taskEXIT_CRITICAL();
}
Menu *ui16_create_crt_menu(bool horizontal_blur,bool scanline){return new CrtMenu(horizontal_blur,scanline);}
#endif
