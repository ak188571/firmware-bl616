#ifdef UI16_SYSTEM_MENU
#include "ui16_crt.h"
#include "menu_manager.h"
#include "utils.h"

namespace {
Ui16CrtSettings settings;
bool initial_from_csv=false;
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
    int osd_width()const override{return 23;}
    void render()override {
        overlay_clear();overlay_cursor(0,0);overlay_printf("%s",scan_supported?"CRT PRESET - STAGE5B":blur_supported?"H BLUR READY - STAGE4A":"BYPASS ONLY - STAGE 1");
        overlay_cursor(0,1);overlay_printf("SESSION ONLY / NO SAVE");
        overlay_cursor(0,2);overlay_printf("INITIAL: %s",initial_from_csv?"CSV":"DEFAULT");
        overlay_cursor(0,3);overlay_printf("%s %s",blur_supported?"CRT ENABLE  ":"CRT REQUEST ",settings.enabled?"ON":"OFF");
        overlay_cursor(0,4);overlay_printf("%s %s",blur_supported?"H BLUR      ":"H BLUR REQ  ",strength(settings.blur));
        overlay_cursor(0,5);if(scan_supported)overlay_printf("SCANLINE     %s",strength(settings.scanline));
        else if(blur_supported)overlay_printf("SCANLINE     NOT YET");
        else overlay_printf("SCANLINE REQ %s",strength(settings.scanline));
        overlay_cursor(0,7);overlay_printf("APPLY: %s",matched?"FRAME ACK":"WAITING");
        overlay_cursor(0,9);overlay_printf("CLOSE");
    }
    std::vector<int> get_options()override{if(blur_supported&&!scan_supported)return {3,4,9};return {3,4,5,9};}
    bool on_adjust(int row,int delta)override {
        if(row==0)settings.enabled=!settings.enabled;
        else if(row==1)settings.blur=(settings.blur+(delta>0?1:3))%4;
        else if(row==2&&(!blur_supported||scan_supported))settings.scanline=(settings.scanline+(delta>0?1:3))%4;
        else return false;
        matched=false;send_settings();return true;
    }
    bool on_choose(int row)override {
        if(row==((blur_supported&&!scan_supported)?2:3)){overlay(0);return true;}
        on_adjust(row,1);redraw=true;return false;
    }
    bool inline_choice(int row)const override{return row<((blur_supported&&!scan_supported)?2:3);}
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
void ui16_crt_new_session(const Ui16CrtRecommendation &recommended){
    initial_from_csv=recommended.present&&recommended.settings.blur<=3&&recommended.settings.scanline<=3;
    settings=initial_from_csv?recommended.settings:Ui16CrtSettings{};
    send_settings();
}
void ui16_crt_accept_status(std::uint32_t applied) {
    taskENTER_CRITICAL();applied_status=applied;status_seen=true;taskEXIT_CRITICAL();
}
Menu *ui16_create_crt_menu(bool horizontal_blur,bool scanline){return new CrtMenu(horizontal_blur,scanline);}
#endif
