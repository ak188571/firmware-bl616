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
int _overlay_on=1,critical_depth=0;
void enter_critical(){++critical_depth;}void leave_critical(){--critical_depth;}
void fpga_tx_header(int c,int n){assert(c==0x12&&n==1);}
void fpga_tx_byte(std::uint8_t){}
std::uint32_t get_core_config(){return config;}
void set_core_config(std::uint32_t v){config=v;++config_sends;}
std::uint64_t bflb_mtimer_get_time_ms(){return now;}
void delay(unsigned ms){now+=ms;}
void get_joypad_states(std::uint16_t *a,std::uint16_t *b,std::uint16_t *c,std::uint16_t *d){
    assert(at<pads.size());*a=pads[at++];*b=*c=*d=0;
}
int overlay_on(){return _overlay_on;}void overlay(int v){_overlay_on=v;}
void overlay_osd_capture_begin(){ui16_osd_capture_begin();}
void overlay_osd_capture_end(){ui16_osd_capture_end();}
void overlay_osd_window(unsigned x,unsigned y,unsigned w,unsigned h){boxes.push_back({x,y,w,h});}
void overlay_clear(){ui16_osd_capture_clear();}
void overlay_cursor(int x,int y){ui16_osd_capture_cursor(x,y);}
void overlay_printf(const char *fmt,...){char s[256];va_list a;va_start(a,fmt);std::vsnprintf(s,sizeof s,fmt,a);va_end(a);ui16_osd_capture_text(s);}
void dprint(const char *,...){}std::uint8_t key_input(){return 0;}void command_console(){}

static unsigned saves=0;static bool save_ok=true;
static Ui16CrtSettings saved_value;static bool saved_reset=false;
bool save_test(const Ui16RomIdentity &id,const Ui16CrtSettings &value,bool reset){assert(id.valid);++saves;saved_value=value;saved_reset=reset;return save_ok;}
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
    ui16_osd_capture_begin();menu->render();ui16_osd_capture_end();
    const auto &cells=ui16_osd_canvas().cells;
    assert(std::string(cells.data()+7*32,16).find("FRAME ACK")!=std::string::npos);
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
    assert((blur_menu->get_options()==std::vector<int>{3,4,9,10}));
    assert(blur_menu->on_adjust(0,1));assert(config==(preserved|0x10000));
    assert(blur_menu->on_adjust(1,1));assert(config==(preserved|0x30000));
    assert(!blur_menu->on_adjust(2,1));assert(config==(preserved|0x30000));
    ui16_crt_accept_status(config);assert(blur_menu->on_tick(20));
    ui16_osd_capture_begin();blur_menu->render();ui16_osd_capture_end();
    const auto &blur_cells=ui16_osd_canvas().cells;
    assert(std::string(blur_cells.data(),32).find("H BLUR READY")!=std::string::npos);
    assert(std::string(blur_cells.data()+5*32,32).find("NOT YET")!=std::string::npos);
    assert(std::string(blur_cells.data()+7*32,32).find("FRAME ACK")!=std::string::npos);
    _overlay_on=1;assert(!blur_menu->on_choose(2)&&overlay_on());
    ui16_crt_new_session();assert(config==preserved);
    std::unique_ptr<Menu> scan_menu(ui16_create_crt_menu(true,true));
    assert((scan_menu->get_options()==std::vector<int>{3,4,5,9,10}));
    assert(scan_menu->on_adjust(0,1));assert(scan_menu->on_adjust(1,1));
    for(unsigned level=1;level<=4;level++){
        assert(scan_menu->on_adjust(2,1));
        assert(config==(preserved|0x30000|((level%4)<<19)));
    }
    assert(scan_menu->on_adjust(2,-1));assert(config==(preserved|0x1b0000));
    ui16_crt_accept_status(config);assert(scan_menu->on_tick(30));
    ui16_osd_capture_begin();scan_menu->render();ui16_osd_capture_end();
    const auto &scan_cells=ui16_osd_canvas().cells;
    assert(std::string(scan_cells.data(),32).find("CRT DEFAULTS - STAGE5G")!=std::string::npos);
    assert(std::string(scan_cells.data()+5*32,32).find("STRONG")!=std::string::npos);
    assert(std::string(scan_cells.data()+7*32,32).find("FRAME ACK")!=std::string::npos);
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
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+2*32,32).find("INITIAL: CSV")!=std::string::npos);
    assert(std::string(ui16_osd_canvas().cells.data()+7*32,32).find("WAITING")!=std::string::npos);
    ui16_crt_accept_status(config);assert(preset_menu->on_tick(3010));
    assert(preset_menu->on_adjust(1,1));const auto edited=config;
    assert(!preset_menu->on_choose(3));
    preset_menu.reset(ui16_create_crt_menu(true,true));
    assert(config==edited); // The initial preset is not reapplied on menu creation.
    ui16_crt_new_session();assert(config==preserved); // A different game with no CSV is OFF.
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+2*32,32).find("INITIAL: DEFAULT")!=std::string::npos);
    ui16_crt_new_session({false,{true,3,3}});assert(config==preserved);
    ui16_crt_new_session({true,{true,4,2}});assert(config==preserved);
    ui16_crt_new_session({true,{true,1,4}});assert(config==preserved);
    ui16_crt_new_session({true,{false,0,0}});assert(config==preserved);
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+2*32,32).find("INITIAL: CSV")!=std::string::npos);
    ui16_crt_new_session(recommended);assert(config==recommended.settings.merge(preserved));
    const Ui16RomIdentity rom_id{true,Ui16RomVolume::Sd,1,0xabcdef01,65552};
    ui16_crt_new_session(recommended,rom_id);
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+8*32,32).find("ROM ID: abcdef01")!=std::string::npos);
    assert(!preset_menu->on_choose(3));preset_menu.reset(ui16_create_crt_menu(true,true));
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+8*32,32).find("abcdef01")!=std::string::npos);
    ui16_crt_new_session(recommended);assert(config==recommended.settings.merge(preserved));
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+8*32,32).find("UNAVAILABLE")!=std::string::npos);
    const Ui16CrtRestore saved{Ui16CrtReadState::User,{true,{true,3,3}}};
    ui16_crt_new_session(recommended,rom_id,saved);assert(config==saved.override_value.settings.merge(preserved));
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+2*32,32).find("INITIAL: USER")!=std::string::npos);
    assert(std::string(ui16_osd_canvas().cells.data()+6*32,32).find("STORE: USER")!=std::string::npos);
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
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+32,32).find("SAVE: SAVED")!=std::string::npos);
    assert(preset_menu->on_adjust(2,1));save_ok=false;
    assert(!preset_menu->on_choose(3)&&saves==3&&overlay_on());
    assert((preset_menu->get_options()==std::vector<int>{3,4,5,9,10}));
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+32,32).find("SAVE: FAILED")!=std::string::npos);
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
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+32,32).find("UNSAVED")!=std::string::npos);
    assert(preset_menu->on_close()&&config==saved.override_value.settings.merge(preserved)&&saves==before_reset);
    assert(!preset_menu->on_choose(4));assert(!preset_menu->on_choose(3));
    assert(saves==before_reset+1&&saved_reset&&saved_value==recommended.settings&&overlay_on()==1);
    assert(!preset_menu->on_choose(3)&&saves==before_reset+1); // Confirmed Reset does not write twice.
    assert(preset_menu->on_adjust(1,1));assert(preset_menu->on_close());
    assert(config==recommended.settings.merge(preserved));
    // Equal numeric values still need a tombstone to stop pinning future CSV changes.
    ui16_crt_new_session(recommended,rom_id,{Ui16CrtReadState::User,recommended},save_test);
    assert(!preset_menu->on_choose(4));
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+32,32).find("UNSAVED")!=std::string::npos);
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
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+2*32,32).find("INITIAL: SYSTEM")!=std::string::npos);
    assert(std::string(ui16_osd_canvas().cells.data()+11*32,32).find("DEFAULTS: OK")!=std::string::npos);
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
    ui16_osd_capture_begin();preset_menu->render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+11*32,32).find("BAD FILE")!=std::string::npos);
    ui16_crt_new_session(recommended,rom_id,saved,save_test,broken_defaults);assert(config==saved.override_value.settings.merge(preserved));
    assert(!preset_menu->on_choose(4)&&config==recommended.settings.merge(preserved));
    broken_defaults=system_defaults;broken_defaults.recommendation.settings.blur=4;
    ui16_crt_new_session({},rom_id,{},save_test,broken_defaults);assert(config==preserved);
    // SNES stage1 has no storage identity, recommendation, or save actions.
    const auto saves_before_preview=saves;
    ui16_crt_preview_session();assert(config==preserved);
    std::unique_ptr<Menu> preview(ui16_create_crt_menu());
    assert((preview->get_options()==std::vector<int>{3,4,5}));
    assert(preview->on_adjust(0,1)&&preview->on_adjust(1,1)&&preview->on_adjust(2,1));
    const auto preview_value=config;
    assert(!preview->on_choose(3)&&!preview->on_choose(4));
    assert(config==preview_value&&saves==saves_before_preview);
    ui16_osd_capture_begin();preview->render();ui16_osd_capture_end();
    for(int row:{1,6,8,11})assert(std::string(ui16_osd_canvas().cells.data()+row*32,32).find("UNAVAILABLE")!=std::string::npos);
    assert(std::string(ui16_osd_canvas().cells.data()+2*32,32).find("DEFAULT")!=std::string::npos);
    assert(preview->on_close()&&config==preserved);
    // Reopening stays volatile; returning to NES restores its existing policy.
    preview.reset(ui16_create_crt_menu());assert((preview->get_options()==std::vector<int>{3,4,5}));
    ui16_crt_new_session(recommended,rom_id,saved,save_test,system_defaults);
    preview.reset(ui16_create_crt_menu(true,true));
    assert((preview->get_options()==std::vector<int>{3,4,5,9,10}));
    assert(config==saved.override_value.settings.merge(preserved));
    assert(!preview->on_choose(3)&&saves==saves_before_preview); // unchanged USER, no write
    assert(critical_depth==0);
    std::puts("CRT host PASS: hotkeys, preserved config, frame ACK/retry, stale ACK rejection, CSV/default/explicit OFF seeds, invalid seed fallback, explicit SAVE/stay-open/retry and X/Y discard of live preview");
}
