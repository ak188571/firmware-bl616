#include "ui16_crt.h"
#include "ui16_osd.h"
#include "ui16_osd_menu.h"
#include "menu_manager.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <memory>
#include <string>

static std::uint32_t config=0xa5e01234;
static std::uint64_t now=0;
static std::vector<std::uint16_t> pads;
static unsigned at=0;
static unsigned config_sends=0;
static std::vector<std::array<unsigned,4>> boxes;
static std::array<char,32*28> hardware;
static std::vector<std::array<char,32*28>> snapshots;
static unsigned cursor_x=0,cursor_y=0;
int _overlay_on=1,critical_depth=0;
void enter_critical(){++critical_depth;}void leave_critical(){--critical_depth;}
void fpga_tx_header(int c,int n){assert(c==0x12&&n==1);}
void fpga_tx_byte(std::uint8_t){}
std::uint32_t get_core_config(){return config;}
void set_core_config(std::uint32_t v){config=v;++config_sends;}
std::uint64_t bflb_mtimer_get_time_ms(){return now;}
void delay(unsigned ms){now+=ms;}
void get_joypad_states(std::uint16_t *a,std::uint16_t *b,std::uint16_t *c,std::uint16_t *d){
    assert(at<pads.size());snapshots.push_back(hardware);*a=pads[at++];*b=*c=*d=0;
}
int overlay_on(){return _overlay_on;}void overlay(int v){_overlay_on=v;}
void overlay_osd_capture_begin(){ui16_osd_capture_begin();}
void overlay_osd_capture_end(){ui16_osd_capture_end();}
void overlay_osd_window(unsigned x,unsigned y,unsigned w,unsigned h){boxes.push_back({x,y,w,h});}
void overlay_clear(){if(!ui16_osd_capture_clear())hardware.fill(' ');}
void overlay_cursor(int x,int y){if(!ui16_osd_capture_cursor(x,y)){cursor_x=x;cursor_y=y;}}
void overlay_printf(const char *fmt,...){
    char s[256];va_list a;va_start(a,fmt);std::vsnprintf(s,sizeof s,fmt,a);va_end(a);
    if(!ui16_osd_capture_text(s))for(const char *p=s;*p;++p){assert(cursor_x<32&&cursor_y<28);hardware[cursor_y*32+cursor_x++]=*p;}
}
void dprint(const char *,...){}std::uint8_t key_input(){return 0;}void command_console(){}

static unsigned saves=0;static bool save_ok=true;
static Ui16CrtSettings saved_value;static bool saved_reset=false;
bool save_test(const Ui16RomIdentity &id,const Ui16CrtSettings &value,bool reset){assert(id.valid);++saves;saved_value=value;saved_reset=reset;return save_ok;}
// Read optional diagnostic fields without changing the default compact view.
static void capture_info(Menu &menu){
    assert(menu.on_info());ui16_osd_capture_begin();menu.render();ui16_osd_capture_end();assert(menu.on_info());
}
static Ui16OsdFrame frame(Menu &menu,Ui16OsdPosition position=Ui16OsdPosition::TopRight,int selected=0){
    ui16_osd_capture_begin();menu.render();ui16_osd_capture_end();int top=0,width=menu.osd_width();
    return ui16_osd_layout(ui16_osd_canvas(),menu.get_options(),selected,top,width,position,true,"CRT SETTINGS",menu.osd_style());
}
static std::string visible(const std::array<char,32*28> &cells,unsigned x,unsigned y,unsigned width,unsigned height){
    std::string text;for(unsigned row=y;row<y+height;++row)text+=std::string(cells.data()+row*32+x,width)+'\n';return text;
}
int main(){
    assert(ui16_osd_request(0x84,0)==Ui16GameOsd::System);
    assert(ui16_osd_request(0,0x44)==Ui16GameOsd::Crt);
    assert(ui16_osd_request(0x184,0)==Ui16GameOsd::System); // extra A
    assert(ui16_osd_request(4,0x40)==Ui16GameOsd::None); // no cross-source chord
    assert(ui16_osd_request(0xc4,0)==Ui16GameOsd::None);
    assert(ui16_osd_request(0x84,0x44)==Ui16GameOsd::None);
    assert(ui16_osd_reserved(0xc4)&&!ui16_osd_reserved(0x40));
    const auto preserved=config&~UI16_CRT_MASK;
    ui16_crt_new_session();assert(config==preserved);
    std::unique_ptr<Menu> menu(ui16_create_crt_menu());
    assert(menu->on_adjust(0,1));assert(config==(preserved|0x10000));
    assert(menu->on_adjust(1,-1));assert(config==(preserved|0x70000));
    assert(menu->on_adjust(2,1));assert(config==(preserved|0xf0000));
    assert(!menu->on_adjust(3,1));
    ui16_crt_accept_status(config);assert(menu->on_tick(10));
    capture_info(*menu);
    const auto &cells=ui16_osd_canvas().cells;
    assert(std::string(cells.data()+8*32,16).find("FRAME ACK")!=std::string::npos);
    // Opening the opposite OSD must neither switch ownership nor adjust a value.
    menu_clear();push_menu(std::move(menu));_overlay_on=1;at=0;boxes.clear();
    pads={0x84,0,0x200};
    ui16_osd_menu_input_loop(false,"CRT SETTINGS",true);
    assert(!overlay_on()&&config==preserved); // X/Y discards the live preview.
    menu_clear();push_menu(std::make_unique<DefaultMenu>());_overlay_on=1;at=0;boxes.clear();
    pads={0x20,0,0x80,0,0x200};
    ui16_osd_menu_input_loop(true,"SYSTEM",true);
    for(auto b:boxes)if(b[2]){assert(b[0]==(32-b[2])/2&&b[1]==(28-b[3])/2);}
    assert(!overlay_on());menu_clear();
    // New game clears only this stage's reserved bits; the session is not saved.
    ui16_crt_new_session();assert(config==preserved&&critical_depth==0);
    // The effect-capable core advertises actual blur and omits scanline control.
    std::unique_ptr<Menu> blur_menu(ui16_create_crt_menu(true));
    assert((blur_menu->get_options()==std::vector<int>{0,1,3,4}));
    assert(blur_menu->on_adjust(0,1));assert(config==(preserved|0x10000));
    assert(blur_menu->on_adjust(1,1));assert(config==(preserved|0x30000));
    assert(!blur_menu->on_adjust(2,1));assert(config==(preserved|0x30000));
    ui16_crt_accept_status(config);assert(blur_menu->on_tick(20));
    capture_info(*blur_menu);
    const auto &blur_cells=ui16_osd_canvas().cells;
    assert(std::string(blur_cells.data()+2*32,32).find("N/A")!=std::string::npos);
    assert(std::string(blur_cells.data()+8*32,32).find("FRAME ACK")!=std::string::npos);
    _overlay_on=1;assert(!blur_menu->on_choose(2)&&overlay_on());
    ui16_crt_new_session();assert(config==preserved);
    std::unique_ptr<Menu> scan_menu(ui16_create_crt_menu(true,true));
    assert((scan_menu->get_options()==std::vector<int>{0,1,2,3,4}));
    assert(scan_menu->on_adjust(0,1));assert(scan_menu->on_adjust(1,1));
    for(unsigned level=1;level<=4;level++){
        assert(scan_menu->on_adjust(2,1));
        assert(config==(preserved|0x30000|((level%4)<<19)));
    }
    assert(scan_menu->on_adjust(2,-1));assert(config==(preserved|0x1b0000));
    ui16_crt_accept_status(config);assert(scan_menu->on_tick(30));
    capture_info(*scan_menu);
    const auto &scan_cells=ui16_osd_canvas().cells;
    assert(std::string(scan_cells.data()+2*32,32).find("STRONG")!=std::string::npos);
    assert(std::string(scan_cells.data()+8*32,32).find("FRAME ACK")!=std::string::npos);
    assert(!scan_menu->on_choose(3)&&overlay_on());
    ui16_crt_new_session();assert(config==preserved);
    // A lost transfer must recover without another controller operation.
    now=1000;ui16_crt_new_session();
    std::unique_ptr<Menu> retry_menu(ui16_create_crt_menu(true,true));
    assert(retry_menu->on_adjust(0,1));assert(retry_menu->on_adjust(1,1));
    const auto desired=config;const unsigned sent=config_sends;
    now=1100;retry_menu->on_tick(now);assert(config_sends==sent);
    now=1300;retry_menu->on_tick(now);assert(config_sends==sent+1&&config==desired);
    ui16_crt_accept_status(desired);now=1310;assert(retry_menu->on_tick(now));
    now=1700;retry_menu->on_tick(now);assert(config_sends==sent+1);
    assert(retry_menu->inline_choice(0)&&retry_menu->inline_choice(2)&&!retry_menu->inline_choice(3)&&retry_menu->inline_choice(4));
    // An old status reply must not acknowledge a newer setting.
    assert(retry_menu->on_adjust(2,1));const auto newer=config;
    ui16_crt_accept_status(desired);now=2000;assert(!retry_menu->on_tick(now));
    assert(config_sends==sent+3&&config==newer);
    ui16_crt_accept_status(newer);now=2010;assert(retry_menu->on_tick(now));
    // CSV values seed only a new session; opening/closing a menu must not reset edits.
    const Ui16CrtRecommendation recommended{true,{true,1,2}};
    now=3000;const auto before_preset=config_sends;
    ui16_crt_new_session(recommended);
    assert(config_sends==before_preset+1&&config==recommended.settings.merge(preserved));
    std::unique_ptr<Menu> preset_menu(ui16_create_crt_menu(true,true));
    assert(!preset_menu->on_tick(3001)); // A previous game's ACK must be cleared.
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+6*32,32).find("INITIAL: CSV")!=std::string::npos);
    assert(std::string(ui16_osd_canvas().cells.data()+8*32,32).find("WAITING")!=std::string::npos);
    ui16_crt_accept_status(config);assert(preset_menu->on_tick(3010));
    assert(preset_menu->on_adjust(1,1));const auto edited=config;
    assert(!preset_menu->on_choose(3));
    preset_menu.reset(ui16_create_crt_menu(true,true));
    assert(config==edited); // The initial preset is not reapplied on menu creation.
    ui16_crt_new_session();assert(config==preserved); // A different game with no CSV is OFF.
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+6*32,32).find("INITIAL: DEFAULT")!=std::string::npos);
    ui16_crt_new_session({false,{true,3,3}});assert(config==preserved);
    ui16_crt_new_session({true,{true,4,2}});assert(config==preserved);
    ui16_crt_new_session({true,{true,1,4}});assert(config==preserved);
    ui16_crt_new_session({true,{false,0,0}});assert(config==preserved);
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+6*32,32).find("INITIAL: CSV")!=std::string::npos);
    ui16_crt_new_session(recommended);assert(config==recommended.settings.merge(preserved));
    const Ui16RomIdentity rom_id{true,Ui16RomVolume::Sd,1,0xabcdef01,65552};
    ui16_crt_new_session(recommended,rom_id);
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+9*32,32).find("ROM ID: abcdef01")!=std::string::npos);
    assert(!preset_menu->on_choose(3));preset_menu.reset(ui16_create_crt_menu(true,true));
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+9*32,32).find("abcdef01")!=std::string::npos);
    ui16_crt_new_session(recommended);assert(config==recommended.settings.merge(preserved));
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+9*32,32).find("UNAVAILABLE")!=std::string::npos);
    const Ui16CrtRestore saved{Ui16CrtReadState::User,{true,{true,3,3}}};
    ui16_crt_new_session(recommended,rom_id,saved);assert(config==saved.override_value.settings.merge(preserved));
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+6*32,32).find("INITIAL: USER")!=std::string::npos);
    assert(std::string(ui16_osd_canvas().cells.data()+7*32,32).find("STORE: USER")!=std::string::npos);
    assert(preset_menu->on_adjust(1,1));const auto restored_edit=config;
    assert(!preset_menu->on_choose(3));preset_menu.reset(ui16_create_crt_menu(true,true));assert(config==restored_edit);
    ui16_crt_new_session(recommended,rom_id,{Ui16CrtReadState::Reset,{}});assert(config==recommended.settings.merge(preserved));
    ui16_crt_new_session({},rom_id,saved);assert(config==saved.override_value.settings.merge(preserved));
    ui16_crt_new_session(recommended,{},saved);assert(config==recommended.settings.merge(preserved));
    ui16_crt_new_session(recommended,rom_id,{Ui16CrtReadState::Bad,saved.override_value});assert(config==recommended.settings.merge(preserved));
    ui16_crt_new_session(recommended,rom_id,{Ui16CrtReadState::User,{true,{true,4,1}}});assert(config==recommended.settings.merge(preserved));
    ui16_crt_new_session(recommended,rom_id,{},save_test);
    preset_menu.reset(ui16_create_crt_menu(true,true));saves=0;_overlay_on=1;
    assert(preset_menu->on_adjust(1,1));assert(preset_menu->on_close()&&saves==0);
    assert(config==recommended.settings.merge(preserved)); // Discard restores actual video settings.
    assert(!preset_menu->on_choose(3)&&saves==1&&overlay_on()); // Explicitly pin even an unchanged recommendation.
    assert(saved_value==recommended.settings);
    assert(!preset_menu->on_choose(3)&&saves==1); // Unchanged saved value causes no extra write.
    assert(preset_menu->on_adjust(1,1));assert(!preset_menu->on_choose(3)&&saves==2&&overlay_on());
    const auto successful=config;
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+5*32,32).find("SAVE: SAVED")!=std::string::npos);
    assert(preset_menu->on_adjust(2,1));save_ok=false;
    assert(!preset_menu->on_choose(3)&&saves==3&&overlay_on());
    assert((preset_menu->get_options()==std::vector<int>{0,1,2,3,4}));
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+5*32,32).find("SAVE: FAILED")!=std::string::npos);
    save_ok=true;assert(!preset_menu->on_choose(3)&&saves==4&&overlay_on()); // Retry stays open too.
    const auto retry_saved=config;
    assert(preset_menu->on_adjust(1,1));assert(config!=retry_saved);
    assert(preset_menu->on_close()&&saves==4&&config==retry_saved);
    assert(retry_saved!=successful);
    // Actual X and Y input paths cancel without invoking storage, even after a failed SAVE.
    for(unsigned back:{0x200u,0x2u}){
        ui16_crt_new_session(recommended,rom_id,{},save_test);save_ok=false;
        auto failing=std::unique_ptr<Menu>(ui16_create_crt_menu(true,true));assert(failing->on_adjust(1,1));
        assert(!failing->on_choose(3));const auto before_back=saves;
        menu_clear();push_menu(std::move(failing));_overlay_on=1;at=0;pads={static_cast<std::uint16_t>(back)};
        ui16_osd_menu_input_loop(false,"CRT SETTINGS",true);
        assert(!overlay_on()&&saves==before_back&&config==recommended.settings.merge(preserved));menu_clear();
    }
    save_ok=true;
    // Reset is a preview until explicit SAVE; cancelling restores values and intent.
    ui16_crt_new_session(recommended,rom_id,saved,save_test);save_ok=true;
    preset_menu.reset(ui16_create_crt_menu(true,true));_overlay_on=1;const auto before_reset=saves;
    assert(!preset_menu->on_choose(4)&&saves==before_reset);
    assert(config==recommended.settings.merge(preserved));
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+5*32,32).find("UNSAVED")!=std::string::npos);
    assert(preset_menu->on_close()&&config==saved.override_value.settings.merge(preserved)&&saves==before_reset);
    assert(!preset_menu->on_choose(4));assert(!preset_menu->on_choose(3));
    assert(saves==before_reset+1&&saved_reset&&saved_value==recommended.settings&&overlay_on()==1);
    assert(!preset_menu->on_choose(3)&&saves==before_reset+1); // Confirmed Reset does not write twice.
    assert(preset_menu->on_adjust(1,1));assert(preset_menu->on_close());
    assert(config==recommended.settings.merge(preserved));
    // Equal numeric values still need a tombstone to stop pinning future CSV changes.
    ui16_crt_new_session(recommended,rom_id,{Ui16CrtReadState::User,recommended},save_test);
    assert(!preset_menu->on_choose(4));
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+5*32,32).find("UNSAVED")!=std::string::npos);
    assert(!preset_menu->on_choose(3)&&saved_reset);
    const Ui16CrtRecommendation changed_csv{true,{false,2,3}};
    ui16_crt_new_session(changed_csv,rom_id,{Ui16CrtReadState::Reset,{},1,9},save_test);
    assert(config==changed_csv.settings.merge(preserved));
    const auto after_reload=saves;assert(!preset_menu->on_choose(3)&&saves==after_reload);
    assert(preset_menu->on_adjust(1,1));assert(!preset_menu->on_choose(3)&&!saved_reset); // Editing after Reset saves an override.
    // No CSV means Reset uses builtin OFF, not the old user settings.
    ui16_crt_new_session({},rom_id,saved,save_test);assert(!preset_menu->on_choose(4)&&config==preserved);
    assert(!preset_menu->on_choose(3)&&saved_reset&&saved_value==Ui16CrtSettings{});
    // Failed Reset SAVE keeps the preview and cancelling through real X/Y restores the user baseline.
    for(unsigned back:{0x200u,0x2u}){
        ui16_crt_new_session(recommended,rom_id,saved,save_test);save_ok=false;
        auto reset_menu=std::unique_ptr<Menu>(ui16_create_crt_menu(true,true));
        assert(!reset_menu->on_choose(4));assert(!reset_menu->on_choose(3));assert(saved_reset);
        const auto writes_before_cancel=saves;
        menu_clear();push_menu(std::move(reset_menu));_overlay_on=1;at=0;pads={static_cast<std::uint16_t>(back)};
        ui16_osd_menu_input_loop(false,"CRT SETTINGS",true);
        assert(!overlay_on()&&saves==writes_before_cancel&&config==saved.override_value.settings.merge(preserved));menu_clear();
    }
    save_ok=true;
    // Lower-priority recommendations are atomic snapshots, overridden by CSV then USER.
    const Ui16CrtDefaults system_defaults{Ui16CrtDefaultsState::Ready,Ui16CrtDefaultsSource::System,{true,{true,2,1}}};
    const Ui16CrtDefaults global_defaults{Ui16CrtDefaultsState::Ready,Ui16CrtDefaultsSource::Global,{true,{true,1,3}}};
    ui16_crt_new_session({},rom_id,{},save_test,system_defaults);assert(config==system_defaults.recommendation.settings.merge(preserved));
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+6*32,32).find("INITIAL: SYSTEM")!=std::string::npos);
    assert(std::string(ui16_osd_canvas().cells.data()+10*32,32).find("DEFAULTS: OK")!=std::string::npos);
    ui16_crt_new_session({},rom_id,{Ui16CrtReadState::Reset,{},0,3},save_test,global_defaults);
    assert(config==global_defaults.recommendation.settings.merge(preserved));
    ui16_crt_new_session(recommended,rom_id,{},save_test,system_defaults);assert(config==recommended.settings.merge(preserved));
    ui16_crt_new_session(recommended,rom_id,saved,save_test,system_defaults);assert(config==saved.override_value.settings.merge(preserved));
    assert(!preset_menu->on_choose(4));assert(config==recommended.settings.merge(preserved));
    assert(preset_menu->on_close()&&config==saved.override_value.settings.merge(preserved));
    ui16_crt_new_session({},rom_id,saved,save_test,system_defaults);assert(!preset_menu->on_choose(4));
    assert(config==system_defaults.recommendation.settings.merge(preserved));
    assert(!preset_menu->on_choose(3)&&saved_reset); // Reset saves mode, not defaults numeric values.
    ui16_crt_new_session({},rom_id,{Ui16CrtReadState::Reset,{},1,4},save_test,global_defaults);
    assert(config==global_defaults.recommendation.settings.merge(preserved)); // A changed defaults file follows on next launch.
    ui16_crt_new_session({true,{false,0,0}},rom_id,{},save_test,system_defaults);assert(config==preserved);
    auto broken_defaults=system_defaults;broken_defaults.state=Ui16CrtDefaultsState::Bad;
    ui16_crt_new_session({},rom_id,{},save_test,broken_defaults);assert(config==preserved);
    capture_info(*preset_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+10*32,32).find("BAD FILE")!=std::string::npos);
    ui16_crt_new_session(recommended,rom_id,saved,save_test,broken_defaults);assert(config==saved.override_value.settings.merge(preserved));
    assert(!preset_menu->on_choose(4)&&config==recommended.settings.merge(preserved));
    broken_defaults=system_defaults;broken_defaults.recommendation.settings.blur=4;
    ui16_crt_new_session({},rom_id,{},save_test,broken_defaults);assert(config==preserved);
    // SNES SD stage4a resolves startup values but owns no write/reset action.
    auto snes_id=rom_id;snes_id.system=2;
    const auto before_readonly=saves;
    for(bool user:{false,true}){
        ui16_crt_readonly_session(recommended,snes_id,user?saved:Ui16CrtRestore{},system_defaults);
        std::unique_ptr<Menu> ro(ui16_create_crt_menu(true,true));
        const auto baseline=config;
        assert(baseline==(user?saved.override_value.settings:recommended.settings).merge(preserved));
        assert((ro->get_options()==std::vector<int>{0,1,2}));
        assert(!ro->on_choose(3)&&!ro->on_choose(4)&&saves==before_readonly&&config==baseline);
        capture_info(*ro);
        const auto &frame=ui16_osd_canvas().cells;
        assert(std::string(frame.data()+5*32,32).find("READ ONLY")!=std::string::npos);
        assert(std::string(frame.data()+6*32,32).find(user?"USER":"CSV")!=std::string::npos);
        assert(std::string(frame.data()+9*32,32).find("UNAVAILABLE")==std::string::npos);
        assert(std::string(frame.data()+10*32,32).find("DEFAULTS: OK")!=std::string::npos);
        assert(!ro->inline_choice(3)&&!ro->inline_choice(4));
        assert(ro->on_adjust(2,1)&&config!=baseline);
        assert(ro->on_close()&&config==baseline&&saves==before_readonly);
    }
    ui16_crt_readonly_session({},snes_id,{},system_defaults);
    assert(config==system_defaults.recommendation.settings.merge(preserved));
    ui16_crt_readonly_session(recommended,rom_id,saved,system_defaults); // Cross-system identity rejected.
    assert(config==recommended.settings.merge(preserved));
    // SNES SD uses the same explicit SAVE and Reset intent semantics.
    ui16_crt_snes_session(recommended,snes_id,saved,save_test,system_defaults);
    std::unique_ptr<Menu> snes_menu(ui16_create_crt_menu(true,true));
    const auto before_snes_save=saves;const auto snes_initial=config;
    assert((snes_menu->get_options()==std::vector<int>{0,1,2,3,4}));
    assert(snes_menu->inline_choice(4)&&config==snes_initial);
    capture_info(*snes_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+4*32,32).find("RESET")!=std::string::npos);
    assert(snes_menu->on_adjust(1,1));_overlay_on=1;save_ok=true;
    assert(!snes_menu->on_choose(3)&&saves==before_snes_save+1&&!saved_reset&&overlay_on());
    const auto snes_success=config;
    assert(!snes_menu->on_choose(3)&&saves==before_snes_save+1); // Same USER value does not write again.
    assert(snes_menu->on_adjust(2,1));save_ok=false;
    assert(!snes_menu->on_choose(3)&&saves==before_snes_save+2&&overlay_on());
    capture_info(*snes_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+5*32,32).find("FAILED")!=std::string::npos);
    assert(snes_menu->on_close()&&config==snes_success); // Failed SAVE cancels to last confirmed value.
    assert(snes_menu->on_adjust(2,1));save_ok=true;
    assert(!snes_menu->on_choose(3)&&saves==before_snes_save+3&&!saved_reset);
    assert(!snes_menu->on_choose(4));
    // Reset previews without writing, cancels values AND intent, and saves mode separately.
    ui16_crt_snes_session(recommended,snes_id,saved,save_test,system_defaults);
    snes_menu.reset(ui16_create_crt_menu(true,true));_overlay_on=1;
    const auto before_snes_reset=saves;
    assert(!snes_menu->on_choose(4)&&saves==before_snes_reset);
    assert(config==recommended.settings.merge(preserved)); // CSV overrides SYSTEM.
    assert(snes_menu->on_close()&&config==saved.override_value.settings.merge(preserved));
    assert(!snes_menu->on_choose(3)&&saves==before_snes_reset); // Cancel restored USER intent.
    assert(!snes_menu->on_choose(4)&&!snes_menu->on_choose(3));
    assert(saves==before_snes_reset+1&&saved_reset&&saved_value==recommended.settings&&overlay_on());
    assert(!snes_menu->on_choose(4)&&!snes_menu->on_choose(3)&&saves==before_snes_reset+1);
    assert(snes_menu->on_adjust(1,1)&&snes_menu->on_close());
    assert(config==recommended.settings.merge(preserved));
    assert(!snes_menu->on_choose(3)&&saves==before_snes_reset+1); // Cancel preserves saved Reset intent.
    assert(snes_menu->on_adjust(1,1)&&!snes_menu->on_choose(3)&&!saved_reset);
    // Even equal numeric USER and recommendation require a Reset-mode save.
    ui16_crt_snes_session(recommended,snes_id,{Ui16CrtReadState::User,recommended},save_test,system_defaults);
    const auto before_equal=saves;
    assert(!snes_menu->on_choose(4)&&config==recommended.settings.merge(preserved));
    capture_info(*snes_menu);
    assert(std::string(ui16_osd_canvas().cells.data()+5*32,32).find("UNSAVED")!=std::string::npos);
    assert(!snes_menu->on_choose(3)&&saved_reset&&saves==before_equal+1);
    // On the next launch a Reset record follows current CSV/SYSTEM/GLOBAL/builtin.
    const Ui16CrtRestore reset_record{Ui16CrtReadState::Reset,{},1,4};
    for(unsigned source=0;source<4;source++){
        const auto csv=source==0?changed_csv:Ui16CrtRecommendation{};
        const auto defs=source==1?system_defaults:source==2?global_defaults:Ui16CrtDefaults{};
        ui16_crt_snes_session(csv,snes_id,reset_record,save_test,defs);
        const auto expected=source==0?changed_csv.settings:source==1?system_defaults.recommendation.settings:
                            source==2?global_defaults.recommendation.settings:Ui16CrtSettings{};
        assert(config==expected.merge(preserved));const auto before_reloaded=saves;
        assert(!snes_menu->on_choose(3)&&saves==before_reloaded);
        capture_info(*snes_menu);
        assert(std::string(ui16_osd_canvas().cells.data()+6*32,32).find(source==0?"CSV":source==1?"SYSTEM":source==2?"GLOBAL":"DEFAULT")!=std::string::npos);
        assert(std::string(ui16_osd_canvas().cells.data()+7*32,32).find("RESET")!=std::string::npos);
    }
    // A failed Reset SAVE cancels through the real X/Y paths to the prior USER.
    for(unsigned back:{0x200u,0x2u}){
        ui16_crt_snes_session(recommended,snes_id,saved,save_test,system_defaults);save_ok=false;
        auto failed_reset=std::unique_ptr<Menu>(ui16_create_crt_menu(true,true));
        assert(!failed_reset->on_choose(4)&&!failed_reset->on_choose(3)&&saved_reset);
        const auto before_cancel=saves;
        menu_clear();push_menu(std::move(failed_reset));_overlay_on=1;at=0;pads={static_cast<std::uint16_t>(back)};
        ui16_osd_menu_input_loop(false,"CRT SETTINGS",true);
        assert(!overlay_on()&&saves==before_cancel&&config==saved.override_value.settings.merge(preserved));menu_clear();
    }
    save_ok=true;
    for(unsigned bad=0;bad<4;bad++){
        auto rejected=snes_id;
        if(bad==0)rejected.valid=false;
        if(bad==1)rejected.system=1;
        if(bad==2)rejected.volume=Ui16RomVolume::Usb;
        ui16_crt_snes_session(recommended,rejected,saved,bad==3?nullptr:save_test,system_defaults);
        snes_menu.reset(ui16_create_crt_menu(true,true));
        assert((snes_menu->get_options()==std::vector<int>{0,1,2}));
        const auto no_write=saves;
        assert(!snes_menu->on_choose(3)&&!snes_menu->on_choose(4)&&saves==no_write);
    }
    // SNES stage1 has no storage identity, recommendation, or save actions.
    const auto saves_before_preview=saves;
    ui16_crt_preview_session();assert(config==preserved);
    std::unique_ptr<Menu> preview(ui16_create_crt_menu());
    assert((preview->get_options()==std::vector<int>{0,1,2}));
    assert(preview->on_adjust(0,1)&&preview->on_adjust(1,1)&&preview->on_adjust(2,1));
    const auto preview_value=config;
    assert(!preview->on_choose(3)&&!preview->on_choose(4));
    assert(config==preview_value&&saves==saves_before_preview);
    capture_info(*preview);
    for(int row:{5,7,9,10})assert(std::string(ui16_osd_canvas().cells.data()+row*32,32).find("UNAVAILABLE")!=std::string::npos);
    assert(std::string(ui16_osd_canvas().cells.data()+6*32,32).find("DEFAULT")!=std::string::npos);
    assert(preview->on_close()&&config==preserved);
    // Reopening stays volatile; returning to NES restores its existing policy.
    preview.reset(ui16_create_crt_menu());assert((preview->get_options()==std::vector<int>{0,1,2}));
    preview.reset(ui16_create_crt_menu(true,false));
    assert((preview->get_options()==std::vector<int>{0,1}));
    assert(!preview->on_adjust(2,1)&&!preview->on_choose(2)&&!preview->on_choose(3));
    assert(config==preserved&&saves==saves_before_preview);
    capture_info(*preview);
    assert(std::string(ui16_osd_canvas().cells.data()+2*32,32).find("N/A")!=std::string::npos);
    assert(preview->on_adjust(0,1)&&preview->on_adjust(1,1));
    assert(preview->on_close()&&config==preserved&&saves==saves_before_preview);
    ui16_crt_new_session(recommended,rom_id,saved,save_test,system_defaults);
    preview.reset(ui16_create_crt_menu(true,true));
    assert((preview->get_options()==std::vector<int>{0,1,2,3,4}));
    assert(config==saved.override_value.settings.merge(preserved));
    assert(!preview->on_choose(3)&&saves==saves_before_preview); // unchanged USER, no write
    assert(critical_depth==0);
    // Compact/full views share the same action indices and never edit/save by switching.
    for(unsigned system:{1u,2u})for(unsigned packed=0;packed<32;++packed){
        auto id=rom_id;id.system=system;
        const Ui16CrtSettings value{bool(packed&1),(packed>>1)&3,(packed>>3)&3};
        const Ui16CrtRestore user{Ui16CrtReadState::User,{true,value}};
        if(system==2)ui16_crt_snes_session({},id,user,save_test,{});else ui16_crt_new_session({},id,user,save_test);
        std::unique_ptr<Menu> compact(ui16_create_crt_menu(true,true));
        ui16_crt_accept_status(config);compact->on_tick(static_cast<std::uint32_t>(now));
        const auto no_edit=config;const auto no_save=saves;
        for(unsigned pos=0;pos<5;++pos){
            const auto small=frame(*compact,static_cast<Ui16OsdPosition>(pos));
            assert(small.width==16&&small.height==10&&small.x+small.width<=31&&small.y+small.height<=27);
            assert(small.width*small.height*100<26*17*40); // More than 60% smaller than the previous normal window.
            const auto text=visible(small.cells,small.x,small.y,small.width,small.height);
            assert(text.find("CRT X/Y:BACK")!=std::string::npos&&text.find("START:INFO")!=std::string::npos);
            assert(text.find("SAVE OK")!=std::string::npos&&text.find("RESET")!=std::string::npos);
            for(const char *hidden:{"ROM ID","INITIAL","STORE:","APPLY:","DEFAULTS:","STAGE"})assert(text.find(hidden)==std::string::npos);
            if(value.blur==2||value.scanline==2)assert(text.find("STD")!=std::string::npos&&text.find("STANDARD")==std::string::npos);
            assert(compact->on_info());const auto expanded=frame(*compact,static_cast<Ui16OsdPosition>(pos));
            assert(expanded.width==24&&expanded.height==17);
            const auto info=visible(expanded.cells,expanded.x,expanded.y,expanded.width,expanded.height);
            for(const char *shown:{"ROM ID: abcdef01","INITIAL: USER","STORE: USER","APPLY: FRAME ACK","DEFAULTS: MISSING","RESET = RECOMMENDED","START:HIDE"})assert(info.find(shown)!=std::string::npos);
            assert(compact->on_info());const auto shrunk=frame(*compact,static_cast<Ui16OsdPosition>(pos));
            assert(shrunk.width==small.width&&shrunk.height==small.height&&config==no_edit&&saves==no_save);
        }
        if(packed==0){
            assert(compact->on_adjust(1,1));auto small=frame(*compact);
            assert(visible(small.cells,small.x,small.y,small.width,small.height).find("SAVE *")!=std::string::npos);
            save_ok=false;assert(!compact->on_choose(3));small=frame(*compact);
            assert(small.width==16&&small.height==10);
            assert(visible(small.cells,small.x,small.y,small.width,small.height).find("SAVE FAILED")!=std::string::npos);
            assert(compact->on_close()&&config==no_edit);save_ok=true;
        }
    }
    // Replay actual text RAM writes: START hold toggles once, retains SAVE selection,
    // keeps the window visible and restores the small geometry on the second press.
    for(unsigned system:{1u,2u}){
        now=10000;auto id=rom_id;id.system=system;
        if(system==2)ui16_crt_snes_session({},id,saved,save_test,{});else ui16_crt_new_session({},id,saved,save_test);
        auto popup=std::unique_ptr<Menu>(ui16_create_crt_menu(true,true));
        ui16_crt_accept_status(config);popup->on_tick(static_cast<std::uint32_t>(now));
        const auto baseline=config;const auto writes_before=saves;const auto sends_before=config_sends;
        menu_clear();push_menu(std::move(popup));_overlay_on=1;at=0;boxes.clear();snapshots.clear();hardware.fill('?');
        pads={0x20,0,0x20,0,0x20,0,8,8,0,8,8,0,0x200};
        ui16_osd_menu_input_loop(false,"CRT SETTINGS",true);
        assert(boxes.size()==3&&boxes[0][2]==16&&boxes[0][3]==10&&boxes[1][2]==24&&boxes[1][3]==17&&boxes[2]==boxes[0]);
        const auto a=boxes[0],b=boxes[1];
        auto initial=visible(snapshots[6],a[0],a[1],a[2],a[3]);
        auto detail=visible(snapshots[7],b[0],b[1],b[2],b[3]);
        auto returned=visible(snapshots[10],a[0],a[1],a[2],a[3]);
        assert(initial.find(">SAVE OK")!=std::string::npos&&detail.find(">SAVE OK")!=std::string::npos&&returned==initial);
        assert(detail.find("ROM ID:")!=std::string::npos&&initial.find("ROM ID:")==std::string::npos);
        assert(!overlay_on()&&config==baseline&&saves==writes_before&&config_sends==sends_before);menu_clear();
        for(unsigned mixed:{0x109u,0x88u}){
            now=20000;ui16_crt_new_session({},id,saved,save_test);
            auto guarded=std::unique_ptr<Menu>(ui16_create_crt_menu(true,true));
            assert(guarded->on_adjust(1,1));ui16_crt_accept_status(config);guarded->on_tick(static_cast<std::uint32_t>(now));
            const auto saves_before_mix=saves;const auto sends_before_mix=config_sends;
            menu_clear();push_menu(std::move(guarded));_overlay_on=1;at=0;boxes.clear();snapshots.clear();
            pads=mixed==0x109u?std::vector<std::uint16_t>{0x20,0,0x20,0,0x20,0,static_cast<std::uint16_t>(mixed),static_cast<std::uint16_t>(mixed),0,0x200}:
                                std::vector<std::uint16_t>{static_cast<std::uint16_t>(mixed),static_cast<std::uint16_t>(mixed),0,0x200};
            ui16_osd_menu_input_loop(false,"CRT SETTINGS",true);
            assert(boxes.size()==1&&saves==saves_before_mix&&config_sends==sends_before_mix+1&&config==baseline);menu_clear();
        }
    }
    std::puts("CRT host PASS: hotkeys, preserved config, frame ACK/retry, stale ACK rejection, CSV/default/explicit OFF seeds, invalid seed fallback, explicit SAVE/stay-open/retry and X/Y discard of live preview");
    std::puts("Compact OSD2 PASS: NES/SNES all 32 settings and 5 positions, hidden diagnostics, SAVE status, START hold/combinations, preserved selection, visible window and exact shrink-back");
}
