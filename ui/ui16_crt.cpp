#ifdef UI16_SYSTEM_MENU
#include "ui16_crt.h"
#include "ui16_osd.h"
#include "menu_manager.h"
#include "utils.h"

namespace {
Ui16CrtSettings settings;
const char *recommended_source="BUILT-IN";
Ui16CrtDefaults defaults_snapshot;
Ui16RomIdentity identity;
Ui16CrtRestore restored;
Ui16CrtSettings persisted,session_recommended;
bool persisted_reset=false,reset_preview=false;
bool preview_only=false;
bool read_only=false;
bool storage_actions_disabled(){return preview_only||read_only;}
bool reset_available(){return !storage_actions_disabled();}
bool dirty(){return settings!=persisted||reset_preview!=persisted_reset;}
Ui16CrtSaveFn save_callback=nullptr;
bool saved_user=false,save_failed=false;
volatile std::uint32_t applied_status=0;
volatile bool status_seen=false;
std::uint32_t last_send_ms=0;
Ui16CrtWidthsFn send_widths=nullptr;
bool glow_supported=false,curve_supported=false,convergence_supported=false,ntsc_supported=false,bloom_supported=false,persistence_supported=false;
void send_settings() {
    last_send_ms=static_cast<std::uint32_t>(bflb_mtimer_get_time_ms());
    taskENTER_CRITICAL();status_seen=false;taskEXIT_CRITICAL();
    if(send_widths)send_widths(settings.widths);
    set_core_config(settings.merge(get_core_config(),send_widths!=nullptr,glow_supported,curve_supported,convergence_supported,ntsc_supported,bloom_supported,persistence_supported));
}
const char *strength(unsigned n) {
    static const char *const names[]={"OFF","WEAK","STD","STRONG"};
    return names[n&3u];
}
struct CrtMenu:Menu {
    explicit CrtMenu(bool blur,bool scan):blur_supported(blur),scan_supported(blur&&scan),extended(send_widths!=nullptr){}
    const bool blur_supported,scan_supported,extended;
    std::uint32_t last_poll=0;
    bool matched=false;
    bool info=false;
    enum Page { Main, Effects, Crop };
    Page page=Main;
    int signal_option()const{return (storage_actions_disabled()?4:6)-int(blur_supported&&!scan_supported);}
    int osd_width()const override{return info?21:13;}
    const Ui16OsdStyle *osd_style()const override{
        static const Ui16OsdStyle normal{13,"CRT X/Y:BACK","START:INFO",true};
        static const Ui16OsdStyle details{13,"CRT X/Y:BACK","START:HIDE",true};
        return info?&details:&normal;
    }
    bool on_info(std::uint16_t buttons)override{
        if(buttons==0x0008){info=!info;redraw=true;}
        return true; // Consume START combinations without editing or saving.
    }
    void render()override {
        overlay_clear();
        int y=0;
        if(extended){
            static const char *const names[]={"MAIN","EFFECTS","OVERSCAN"};
            overlay_cursor(0,y++);overlay_printf("PAGE %d/3 %s",int(page)+1,names[page]);
        }
        if(page==Main){
            overlay_cursor(0,y++);overlay_printf("%s %s",blur_supported?"CRT  ":"CRT REQ",settings.enabled?"ON":"OFF");
            overlay_cursor(0,y++);overlay_printf("BLUR %s",strength(extended?settings.signal_blur():settings.blur));
            overlay_cursor(0,y++);overlay_printf("SCAN %s",blur_supported&&!scan_supported?"N/A":strength(settings.scanline));
            overlay_cursor(0,y++);overlay_printf("%s",storage_actions_disabled()?"SAVE N/A":save_failed?"SAVE FAILED":dirty()?"SAVE *":(saved_user||persisted_reset)?"SAVE OK":"SAVE");
            overlay_cursor(0,y++);overlay_printf("%s",reset_available()?"RESET":"RESET N/A");
            if(extended){overlay_cursor(0,y++);overlay_printf("SIGNAL %s",settings.ntsc && ntsc_supported?"NTSC":settings.composite?"COMPOSITE":"RGB");}
        }else if(page==Effects){
            static const char *const masks[]={"OFF","APERTURE GRILLE","SHADOW MASK"};
            static const char *const gamma[]={"OFF","0.80","1.25"};
            static const char *const temperatures[]={"OFF","WARM","COOL"};
            overlay_cursor(0,y++);overlay_printf("BEAM SCAN %s",settings.beam?"ON":"OFF");
            overlay_cursor(0,y++);overlay_printf("MASK %s",masks[settings.mask]);
            overlay_cursor(0,y++);overlay_printf("GAMMA %s",gamma[settings.gamma]);
            overlay_cursor(0,y++);overlay_printf("WHITE POINT %s",temperatures[settings.temperature]);
            overlay_cursor(0,y++);overlay_printf("VIGNETTE %s",settings.vignette?"ON":"OFF");
            overlay_cursor(0,y++);overlay_printf("CORNERS %s",settings.rounded?"ON":"OFF");
            overlay_cursor(0,y++);overlay_printf("CHROMA BLEED %s",settings.signal_bleed()?"ON":"OFF");
            if(glow_supported){overlay_cursor(0,y++);overlay_printf("GLOW %s",settings.glow?"ON":"OFF");}
            if(curve_supported){overlay_cursor(0,y++);overlay_printf("CURVATURE %s",settings.curvature?"ON":"OFF");}
            if(convergence_supported){overlay_cursor(0,y++);overlay_printf("CONVERGENCE %s",settings.convergence?"ON":"OFF");}
            if(bloom_supported){overlay_cursor(0,y++);overlay_printf("BLOOM %s",settings.bloom?"ON":"OFF");}
            if(persistence_supported){overlay_cursor(0,y++);overlay_printf("AFTERGLOW %s",settings.persistence?"ON":"OFF");}
        }else{
            static const char *const edges[]={"TOP","BOTTOM","LEFT","RIGHT"};
            overlay_cursor(0,y++);overlay_printf("OVERSCAN %s",settings.overscan?"ON":"OFF");
            for(unsigned i=0;i<4;++i){
                overlay_cursor(0,y++);overlay_printf("%s %u PX",edges[i],unsigned(settings.widths[i]));
            }
        }
        if(!info)return;
        overlay_cursor(0,y++);overlay_printf("SAVE: %s",read_only?"READ ONLY":preview_only?"UNAVAILABLE":save_failed?"FAILED":dirty()?"UNSAVED":(saved_user||persisted_reset)?"SAVED":"RECOMMENDED");
        // These describe the ROM-load snapshot; SAVE above is the live state.
        overlay_cursor(0,y++);overlay_printf("LOADED FROM: %s",restored.override_value.present?"USER":recommended_source);
        overlay_cursor(0,y++);overlay_printf("SAVE READ: %s",preview_only?"UNAVAILABLE":ui16_crt_read_name(restored.state));
        overlay_cursor(0,y++);overlay_printf("%s: %s",extended?"CONFIG":"APPLY",matched?"FRAME ACK":"WAITING");
        overlay_cursor(0,y++);if(identity.valid)overlay_printf("ROM ID: %08lx",static_cast<unsigned long>(identity.crc));
        else overlay_printf("ROM ID: UNAVAILABLE");
        overlay_cursor(0,y++);overlay_printf("DEFAULTS: %s",ui16_crt_defaults_state_name(defaults_snapshot.state));
        overlay_cursor(0,y);overlay_printf("RESET = RECOMMENDED");
    }
    std::vector<int> get_options()override{
        if(page==Effects){
            std::vector<int> rows{0,1,2,3,4,5,6,7};
            if(glow_supported)rows.push_back(8);
            if(curve_supported)rows.push_back(9);
            if(convergence_supported)rows.push_back(10);
            if(bloom_supported)rows.push_back(11);
            if(persistence_supported)rows.push_back(12);
            return rows;
        }
        if(page==Crop)return {0,1,2,3,4,5};
        std::vector<int> rows;
        if(extended)rows.push_back(0);
        for(int i=0;i<(storage_actions_disabled()?3:5);++i)
            if(i!=2 || !blur_supported || scan_supported)rows.push_back(i+int(extended));
        if(extended)rows.push_back(6);
        return rows;
    }
    bool on_adjust(int row,int delta)override {
        if(extended && row==0){
            page=static_cast<Page>((int(page)+(delta>0?1:2))%3);
            redraw=true;return true;
        }
        if(extended)--row;
        const unsigned step=delta>0?1:2;
        if(page==Effects){
            switch(row){
            case 0: settings.beam=!settings.beam;break;
            case 1: settings.mask=(settings.mask+step)%3;break;
            case 2: settings.gamma=(settings.gamma+step)%3;break;
            case 3: settings.temperature=(settings.temperature+step)%3;break;
            case 4: settings.vignette=!settings.vignette;break;
            case 5: settings.rounded=!settings.rounded;break;
            case 6: if(settings.composite)settings.composite_bleed=!settings.composite_bleed;
                    else settings.bleed=!settings.bleed;
                    break;
            case 7: if(!glow_supported)return false;settings.glow=!settings.glow;break;
            case 8: if(!curve_supported)return false;settings.curvature=!settings.curvature;break;
            case 11: if(!persistence_supported)return false;settings.persistence=!settings.persistence;break;
            case 10: if(!bloom_supported)return false;settings.bloom=!settings.bloom;break;
            case 9: if(!convergence_supported)return false;settings.convergence=!settings.convergence;break;
            default:return false;
            }
        }else if(page==Crop){
            if(row==0)settings.overscan=!settings.overscan;
            else if(row>=1 && row<=4)settings.widths[row-1]+=delta>0?1:-1;
            else return false;
        }else if(row==0)settings.enabled=!settings.enabled;
        else if(row==1){
            auto &blur=(extended && settings.composite)?settings.composite_blur:settings.blur;
            blur=(blur+(delta>0?1:3))%4;
        }else if(extended && row==signal_option()-1){
            // RGB and both composite modes share geometry; the two composite
            // modes use the same blur/bleed bank so their signal paths can be compared.
            const int count=ntsc_supported?3:2;
            const int current=settings.ntsc && ntsc_supported?2:int(settings.composite);
            const int next=(current+(delta>0?1:count-1))%count;
            settings.composite=next!=0;settings.ntsc=next==2;
        }
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
        if(extended && (row==0 || page!=Main || row==signal_option())){on_adjust(row,1);redraw=true;return false;}
        const int option=row;
        if(extended)--row;
        const int save_row=(blur_supported&&!scan_supported)?2:3;
        if(storage_actions_disabled()&&row>=save_row)return false;
        if(row==save_row+1){
            if(!reset_available())return false;
            settings=session_recommended;reset_preview=true;save_failed=false;
            matched=false;send_settings();redraw=true;return false;
        }
        if(row==save_row){
            if(!dirty()&&(saved_user||persisted_reset))save_failed=false;
            else if(!save_callback||!save_callback(identity,settings,reset_preview))save_failed=true;
            else {persisted=settings;persisted_reset=reset_preview;saved_user=!reset_preview;save_failed=false;}
            redraw=true;return false; // SAVE never dismisses the OSD.
        }
        on_adjust(option,1);redraw=true;return false;
    }
    bool inline_choice(int row)const override{
        if(page!=Main || (extended && row==signal_option()))return true;
        const int save_row=(blur_supported&&!scan_supported?2:3)+int(extended);
        return row<save_row||(reset_available()&&row==save_row+1);
    }
    bool on_tick(std::uint32_t now)override {
        bool seen;std::uint32_t value;
        taskENTER_CRITICAL();seen=status_seen;value=applied_status;taskEXIT_CRITICAL();
        const bool next=seen && value==settings.merge(value,extended,glow_supported,curve_supported,convergence_supported,ntsc_supported,bloom_supported,persistence_supported);
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
void ui16_crt_set_width_sender(Ui16CrtWidthsFn send,bool supports_glow,bool supports_curve,bool supports_convergence,bool supports_ntsc,bool supports_bloom,bool supports_persistence){
    send_widths=send;glow_supported=send && supports_glow;curve_supported=glow_supported && supports_curve;
    convergence_supported=curve_supported && supports_convergence;
    ntsc_supported=convergence_supported && supports_ntsc;
    bloom_supported=ntsc_supported && supports_bloom;
    persistence_supported=bloom_supported && supports_persistence;
}
void ui16_crt_new_session(const Ui16CrtRecommendation &recommended,const Ui16RomIdentity &rom_id,const Ui16CrtRestore &restore,Ui16CrtSaveFn save,const Ui16CrtDefaults &defaults){
    preview_only=false;read_only=false;
    identity=rom_id;save_callback=save;save_failed=false;
    restored=restore;
    if(!identity.valid||!restore.override_value.present||!restore.override_value.settings.valid()||
       (restore.state!=Ui16CrtReadState::User&&restore.state!=Ui16CrtReadState::Recovered))restored.override_value={};
    defaults_snapshot=defaults;
    recommended_source="BUILT-IN";session_recommended={};
    if(defaults.state==Ui16CrtDefaultsState::Ready&&defaults.recommendation.present&&
       defaults.recommendation.settings.valid()&&
       (defaults.source==Ui16CrtDefaultsSource::System||defaults.source==Ui16CrtDefaultsSource::Global)){
        session_recommended=defaults.recommendation.settings;
        recommended_source=defaults.source==Ui16CrtDefaultsSource::System?"SYSTEM":"GLOBAL";
    }
    if(recommended.present&&recommended.settings.valid()){
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
void ui16_crt_snes_session(const Ui16CrtRecommendation &recommended,const Ui16RomIdentity &rom_id,
                              const Ui16CrtRestore &restore,Ui16CrtSaveFn save,const Ui16CrtDefaults &defaults){
    if(!rom_id.valid||rom_id.system!=2||rom_id.volume!=Ui16RomVolume::Sd||!save){
        ui16_crt_readonly_session(recommended,rom_id,restore,defaults);return;
    }
    ui16_crt_new_session(recommended,rom_id,restore,save,defaults);
}
Menu *ui16_create_crt_menu(bool horizontal_blur,bool scanline){return new CrtMenu(horizontal_blur,scanline);}
#endif
