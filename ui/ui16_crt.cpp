#ifdef UI16_SYSTEM_MENU
#include "ui16_crt.h"
#include "menu_manager.h"
#include "utils.h"

namespace {
Ui16CrtSettings settings;
bool initial_from_csv=false;
Ui16RomIdentity identity;
Ui16CrtRestore restored;
Ui16CrtSettings persisted;
Ui16CrtSaveFn save_callback=nullptr;
bool saved_user=false,save_failed=false;
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
        overlay_clear();overlay_cursor(0,0);overlay_printf("%s",scan_supported?"CRT SAVE - STAGE5E":blur_supported?"H BLUR READY - STAGE4A":"BYPASS ONLY - STAGE 1");
        overlay_cursor(0,1);overlay_printf("SAVE: %s",save_failed?"FAILED":settings!=persisted?"UNSAVED":saved_user?"SAVED":"RECOMMENDED");
        overlay_cursor(0,2);overlay_printf("INITIAL: %s",restored.override_value.present?"USER":initial_from_csv?"CSV":"DEFAULT");
        overlay_cursor(0,3);overlay_printf("%s %s",blur_supported?"CRT ENABLE  ":"CRT REQUEST ",settings.enabled?"ON":"OFF");
        overlay_cursor(0,4);overlay_printf("%s %s",blur_supported?"H BLUR      ":"H BLUR REQ  ",strength(settings.blur));
        overlay_cursor(0,5);if(scan_supported)overlay_printf("SCANLINE     %s",strength(settings.scanline));
        else if(blur_supported)overlay_printf("SCANLINE     NOT YET");
        else overlay_printf("SCANLINE REQ %s",strength(settings.scanline));
        overlay_cursor(0,6);overlay_printf("STORE: %s",ui16_crt_read_name(restored.state));
        overlay_cursor(0,7);overlay_printf("APPLY: %s",matched?"FRAME ACK":"WAITING");
        overlay_cursor(0,8);if(identity.valid)overlay_printf("ROM ID: %08lx",static_cast<unsigned long>(identity.crc));
        else overlay_printf("ROM ID: UNAVAILABLE");
        overlay_cursor(0,9);overlay_printf("CLOSE");
        if(save_failed){overlay_cursor(0,10);overlay_printf("LEAVE UNSAVED");}
    }
    std::vector<int> get_options()override{if(blur_supported&&!scan_supported)return save_failed?std::vector<int>{3,4,9,10}:std::vector<int>{3,4,9};return save_failed?std::vector<int>{3,4,5,9,10}:std::vector<int>{3,4,5,9};}
    bool on_adjust(int row,int delta)override {
        if(row==0)settings.enabled=!settings.enabled;
        else if(row==1)settings.blur=(settings.blur+(delta>0?1:3))%4;
        else if(row==2&&(!blur_supported||scan_supported))settings.scanline=(settings.scanline+(delta>0?1:3))%4;
        else return false;
        save_failed=false;matched=false;send_settings();return true;
    }
    bool on_close()override {
        if(settings==persisted||!save_callback){save_failed=false;return true;}
        if(!save_callback(identity,settings)){save_failed=true;redraw=true;return false;}
        persisted=settings;saved_user=true;save_failed=false;return true;
    }
    bool on_choose(int row)override {
        const int close_row=(blur_supported&&!scan_supported)?2:3;
        if(save_failed&&row==close_row+1){save_failed=false;overlay(0);return true;}
        if(row==close_row){if(!on_close())return false;overlay(0);return true;}
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
void ui16_crt_new_session(const Ui16CrtRecommendation &recommended,const Ui16RomIdentity &rom_id,const Ui16CrtRestore &restore,Ui16CrtSaveFn save){
    identity=rom_id;save_callback=save;save_failed=false;
    restored=restore;
    if(!identity.valid||!restore.override_value.present||restore.override_value.settings.blur>3||restore.override_value.settings.scanline>3||
       (restore.state!=Ui16CrtReadState::User&&restore.state!=Ui16CrtReadState::Recovered))restored.override_value={};
    initial_from_csv=recommended.present&&recommended.settings.blur<=3&&recommended.settings.scanline<=3;
    settings=initial_from_csv?recommended.settings:Ui16CrtSettings{};
    if(restored.override_value.present)settings=restored.override_value.settings;
    persisted=settings;saved_user=restored.override_value.present;
    send_settings();
}
void ui16_crt_accept_status(std::uint32_t applied) {
    taskENTER_CRITICAL();applied_status=applied;status_seen=true;taskEXIT_CRITICAL();
}
Menu *ui16_create_crt_menu(bool horizontal_blur,bool scanline){return new CrtMenu(horizontal_blur,scanline);}
#endif
