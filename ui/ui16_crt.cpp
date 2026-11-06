#ifdef UI16_SYSTEM_MENU
#include "ui16_crt.h"
#include "menu_manager.h"
#include "utils.h"

namespace {
Ui16CrtSettings settings;
const char *recommended_source="DEFAULT";
Ui16CrtDefaults defaults_snapshot;
Ui16RomIdentity identity;
Ui16CrtRestore restored;
Ui16CrtSettings persisted,session_recommended;
bool persisted_reset=false,reset_preview=false;
bool preview_only=false;
bool read_only=false;
bool storage_actions_disabled(){return preview_only||read_only;}
bool dirty(){return settings!=persisted||reset_preview!=persisted_reset;}
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
        overlay_clear();overlay_cursor(0,0);overlay_printf("%s",read_only?"SNES CRT READ ONLY":preview_only&&blur_supported?"SNES CRT PREVIEW":scan_supported?"CRT DEFAULTS - STAGE5G":blur_supported?"H BLUR READY - STAGE4A":"BYPASS ONLY - STAGE 1");
        overlay_cursor(0,1);overlay_printf("SAVE: %s",read_only?"READ ONLY":preview_only?"UNAVAILABLE":save_failed?"FAILED":dirty()?"UNSAVED":(saved_user||persisted_reset)?"SAVED":"RECOMMENDED");
        overlay_cursor(0,2);overlay_printf("INITIAL: %s",restored.override_value.present?"USER":recommended_source);
        overlay_cursor(0,3);overlay_printf("%s %s",blur_supported?"CRT ENABLE  ":"CRT REQUEST ",settings.enabled?"ON":"OFF");
        overlay_cursor(0,4);overlay_printf("%s %s",blur_supported?"H BLUR      ":"H BLUR REQ  ",strength(settings.blur));
        overlay_cursor(0,5);if(scan_supported)overlay_printf("SCANLINE     %s",strength(settings.scanline));
        else if(blur_supported)overlay_printf("SCANLINE     NOT YET");
        else overlay_printf("SCANLINE REQ %s",strength(settings.scanline));
        overlay_cursor(0,6);overlay_printf("STORE: %s",preview_only?"UNAVAILABLE":ui16_crt_read_name(restored.state));
        overlay_cursor(0,7);overlay_printf("APPLY: %s",matched?"FRAME ACK":"WAITING");
        overlay_cursor(0,8);if(identity.valid)overlay_printf("ROM ID: %08lx",static_cast<unsigned long>(identity.crc));
        else overlay_printf("ROM ID: UNAVAILABLE");
        overlay_cursor(0,9);overlay_printf("%s",storage_actions_disabled()?"SAVE: NOT AVAILABLE":"SAVE (X/Y: CANCEL)");
        overlay_cursor(0,10);overlay_printf("%s",storage_actions_disabled()?"RESET: NOT AVAILABLE":"RESET TO RECOMMENDED");
        overlay_cursor(0,11);overlay_printf("DEFAULTS: %s",ui16_crt_defaults_state_name(defaults_snapshot.state));
    }
    std::vector<int> get_options()override{if(storage_actions_disabled()){if(blur_supported&&!scan_supported)return {3,4};return {3,4,5};}if(blur_supported&&!scan_supported)return {3,4,9,10};return {3,4,5,9,10};}
    bool on_adjust(int row,int delta)override {
        if(row==0)settings.enabled=!settings.enabled;
        else if(row==1)settings.blur=(settings.blur+(delta>0?1:3))%4;
        else if(row==2&&(!blur_supported||scan_supported))settings.scanline=(settings.scanline+(delta>0?1:3))%4;
        else return false;
        reset_preview=false;save_failed=false;matched=false;send_settings();return true;
    }
    bool on_close()override {
        // X/Y cancels this preview, including the live FPGA configuration.
        if(settings!=persisted){settings=persisted;matched=false;send_settings();}
        reset_preview=persisted_reset;save_failed=false;return true;
    }
    bool on_choose(int row)override {
        const int save_row=(blur_supported&&!scan_supported)?2:3;
        if(storage_actions_disabled()&&row>=save_row)return false;
        if(row==save_row+1){
            settings=session_recommended;reset_preview=true;save_failed=false;
            matched=false;send_settings();redraw=true;return false;
        }
        if(row==save_row){
            if(!dirty()&&(saved_user||persisted_reset))save_failed=false;
            else if(!save_callback||!save_callback(identity,settings,reset_preview))save_failed=true;
            else {persisted=settings;persisted_reset=reset_preview;saved_user=!reset_preview;save_failed=false;}
            redraw=true;return false; // SAVE never dismisses the OSD.
        }
        on_adjust(row,1);redraw=true;return false;
    }
    bool inline_choice(int row)const override{const int save_row=(blur_supported&&!scan_supported)?2:3;return row<save_row||(!storage_actions_disabled()&&row==save_row+1);}
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
void ui16_crt_new_session(const Ui16CrtRecommendation &recommended,const Ui16RomIdentity &rom_id,const Ui16CrtRestore &restore,Ui16CrtSaveFn save,const Ui16CrtDefaults &defaults){
    preview_only=false;read_only=false;
    identity=rom_id;save_callback=save;save_failed=false;
    restored=restore;
    if(!identity.valid||!restore.override_value.present||restore.override_value.settings.blur>3||restore.override_value.settings.scanline>3||
       (restore.state!=Ui16CrtReadState::User&&restore.state!=Ui16CrtReadState::Recovered))restored.override_value={};
    defaults_snapshot=defaults;
    recommended_source="DEFAULT";session_recommended={};
    if(defaults.state==Ui16CrtDefaultsState::Ready&&defaults.recommendation.present&&
       defaults.recommendation.settings.blur<=3&&defaults.recommendation.settings.scanline<=3&&
       (defaults.source==Ui16CrtDefaultsSource::System||defaults.source==Ui16CrtDefaultsSource::Global)){
        session_recommended=defaults.recommendation.settings;
        recommended_source=defaults.source==Ui16CrtDefaultsSource::System?"SYSTEM":"GLOBAL";
    }
    if(recommended.present&&recommended.settings.blur<=3&&recommended.settings.scanline<=3){
        session_recommended=recommended.settings;recommended_source="CSV";
    }
    settings=session_recommended;
    if(restored.override_value.present)settings=restored.override_value.settings;
    persisted=settings;saved_user=restored.override_value.present;
    persisted_reset=identity.valid&&!saved_user&&(restore.state==Ui16CrtReadState::Reset||
        (restore.state==Ui16CrtReadState::Recovered&&restore.selected_slot>=0));
    reset_preview=persisted_reset;
    send_settings();
}
void ui16_crt_preview_session(){
    ui16_crt_new_session();
    preview_only=true;
    defaults_snapshot.state=Ui16CrtDefaultsState::Unavailable;
}
void ui16_crt_readonly_session(const Ui16CrtRecommendation &recommended,const Ui16RomIdentity &rom_id,
                             const Ui16CrtRestore &restore,const Ui16CrtDefaults &defaults){
    const Ui16RomIdentity approved=rom_id.valid&&rom_id.system==2?rom_id:Ui16RomIdentity{};
    ui16_crt_new_session(recommended,approved,approved.valid?restore:Ui16CrtRestore{},nullptr,defaults);
    read_only=true;
}
void ui16_crt_accept_status(std::uint32_t applied) {
    taskENTER_CRITICAL();applied_status=applied;status_seen=true;taskEXIT_CRITICAL();
}
Menu *ui16_create_crt_menu(bool horizontal_blur,bool scanline){return new CrtMenu(horizontal_blur,scanline);}
#endif
