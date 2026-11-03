#include "ui16_crt_store.h"
#include "ui16_crt.h"
#include "menu_manager.h"
#include "crt_fake_fs.h"
#include <cassert>
#include <memory>
#include <cstdarg>
#include <cstdio>

int critical_depth=0,_overlay_on=1;
static std::uint32_t config=0xabc01234;
static unsigned now=0;
static int line=0;
static std::map<int,std::string> screen;
void enter_critical(){++critical_depth;}void leave_critical(){--critical_depth;}
std::uint32_t get_core_config(){return config;}void set_core_config(std::uint32_t v){config=v;}
std::uint64_t bflb_mtimer_get_time_ms(){return now;}void delay(unsigned ms){now+=ms;}
void fpga_tx_header(int,int){}void fpga_tx_byte(std::uint8_t){}
void overlay_clear(){screen.clear();}void overlay_cursor(int,int y){line=y;}
void overlay_printf(const char *fmt,...){char text[128];va_list a;va_start(a,fmt);std::vsnprintf(text,sizeof text,fmt,a);va_end(a);screen[line]=text;}
void overlay(int v){_overlay_on=v;}
int overlay_on(){return _overlay_on;}

using fake_fs::files;
static void setup(){fake_fs::reset();ui16_crt_end_session();files["sd:nes/game.nes"]="123456789";}
static std::string slot(char suffix){
    for(const auto &p:files)if(p.first.find("/01-cbf43926-0000000000000009.")!=std::string::npos&&p.first.back()==suffix)return p.first;
    return {};
}
static Ui16CrtStore load(){Ui16CrtStore s;s.prepare(1,"sd:nes/game.nes");return s;}
static void checksum(std::string &r){const auto crc=ui16_crt_crc(0xffffffffu,r.data(),28)^0xffffffffu;for(int i=0;i<4;++i)r[28+i]=char(crc>>(8*i));}
int main(){
    assert((ui16_crt_crc(0xffffffffu,"123456789",9)^0xffffffffu)==0xcbf43926u);
    const Ui16CrtSettings off{},weak{true,1,1},strong{true,3,3},system{true,2,0};
    setup();auto s=load();assert(s.ready&&s.initial==off&&!s.dirty(off)&&s.recommended==off);
    files["sd:/crt/defaults.tsv"]="system\tcrt_v1\n*\t1:1:1\nnes\t1:2:0\n";
    s=load();assert(s.initial==system&&std::string(s.recommended_source)=="SYSTEM");
    files["sd:/crt/defaults.tsv"]="system\tcrt_v1\n*\t1:1:1\n";
    s=load();assert(s.initial==weak&&std::string(s.recommended_source)=="GLOBAL");
    files["sd:nes/titles.csv"]="name\ttitle\tcrt_v1\ngame.nes\t日本\t1:3:3\n";
    s=load();assert(s.initial==strong&&std::string(s.recommended_source)=="GAME");
    assert(s.save(weak));s=load();assert(s.user_override&&s.initial==weak&&s.recommended==strong);
    assert(!s.dirty(weak));assert(fake_fs::writes==1); // No write on prepare/reopen.
    files["sd:nes/renamed.nes"]="123456789";
    Ui16CrtStore renamed;renamed.prepare(1,"sd:nes/renamed.nes");assert(renamed.initial==weak&&renamed.user_override);
    files["sd:nes/other.nes"]="123456788";Ui16CrtStore other;other.prepare(1,"sd:nes/other.nes");assert(!other.user_override);
    Ui16CrtStore other_system;other_system.prepare(2,"sd:nes/game.nes");assert(!other_system.user_override);
    files["usb:nes/game.nes"]="123456789";Ui16CrtStore usb;usb.prepare(1,"usb:nes/game.nes");assert(!usb.user_override&&usb.initial==off);
    assert(s.dirty(s.recommended)&&s.save(s.recommended)); // Durable tombstone, not a frozen recommendation.
    files["sd:nes/titles.csv"]="name\ttitle\tcrt_v1\ngame.nes\t日本\t1:2:0\n";
    s=load();assert(!s.user_override&&s.initial==system);assert(s.save(strong));
    auto a=slot('a'),b=slot('b');assert(!a.empty()&&!b.empty());
    files[a].resize(7);s=load();assert(!s.user_override&&s.initial==system&&std::string(s.notice)=="RECOVERED");
    files[b].clear();s=load();assert(s.initial==system&&std::string(s.notice)=="BAD SAVE");
    assert(s.save(weak));const auto good=files;
    const auto good_a=slot('a'),good_b=slot('b');
    for(int byte=0;byte<32;++byte){files=good;files[good_a][byte]^=1;s=load();assert(!s.user_override&&s.initial==system);}
    files=good;files[good_a][12]^=1;checksum(files[good_a]);s=load();assert(!s.user_override); // Size is checked independently of record checksum.
    files=good;files[good_a][5]=2;checksum(files[good_a]);s=load();assert(!s.user_override); // System mismatch.
    files=good;files[good_b]=files[good_a];
    for(int i=0;i<4;++i){files[good_a][24+i]=char(0xff);files[good_b][24+i]=char(i?0xff:0xfe);}
    checksum(files[good_a]);checksum(files[good_b]);s=load();assert(s.save(strong));s=load();assert(s.initial==strong); // Generation wrap.
    // Every possible torn write leaves the preceding valid record usable.
    for(int length=0;length<32;++length){
        files=good;fake_fs::short_write=length;s=load();assert(!s.save(strong));
        fake_fs::short_write=-1;s=load();assert(s.initial==weak&&s.user_override);
    }
    files=good;fake_fs::sync_error=true;s=load();assert(!s.save(strong));fake_fs::sync_error=false;
    // A sync failure can nevertheless leave a complete new slot. Retrying must
    // reread generations rather than overwrite the sole latest valid record.
    assert(s.save(strong));s=load();assert(s.initial==strong);
    fake_fs::corrupt_close=true;assert(!s.save(weak));s=load();assert(s.initial==strong); // Readback catches silent corruption.
    fake_fs::write_result_error=true;assert(!s.save(weak));fake_fs::write_result_error=false;s=load();assert(s.initial==strong);
    fake_fs::write_close_error=true;assert(!s.save(weak));fake_fs::write_close_error=false;assert(s.save(strong));
    fake_fs::write_error=true;assert(!s.save(weak));fake_fs::write_error=false;
    fake_fs::mkdir_error=true;assert(!s.save(weak));fake_fs::mkdir_error=false;
    fake_fs::close_error=true;assert(!s.save(weak));fake_fs::close_error=false;
    assert(fake_fs::handles.empty());
    // Unknown schema with a valid checksum is preserved and blocks writing.
    auto future=files[slot('a')];future[4]=2;checksum(future);files[slot('a')]=future;
    s=load();const auto preserved=files;assert(!s.save(weak)&&files==preserved);
    setup();files["sd:/crt/defaults.tsv"]="system\tcrt_v1\n*\t1:1:1\nnes\t9:0:0\n";
    s=load();assert(s.initial==off&&std::string(s.notice)=="BAD DEFAULTS");
    files["sd:nes/titles.csv"]="name\ttitle\tcrt_v1\ngame.nes\t日本\t1:4:0\n";
    s=load();assert(s.initial==off&&std::string(s.notice)=="BAD TITLES");
    fake_fs::read_error=true;s=load();assert(!s.ready&&!s.save(weak));fake_fs::read_error=false;
    // Actual CRT popup: close saves, reset removes override, failure retains RAM,
    // retry succeeds, and an explicit LEAVE UNSAVED always permits resuming.
    setup();ui16_crt_prepare_game(1,"sd:nes/game.nes");ui16_crt_new_session();
    std::unique_ptr<Menu> menu(ui16_create_crt_menu(true,true));
    assert((menu->get_options()==std::vector<int>{3,4,5,11,13}));
    assert(menu->on_adjust(0,1)&&menu->on_adjust(1,1));const auto requested=config;
    menu->render();assert(screen[8]=="SAVE: UNSAVED");
    fake_fs::write_error=true;assert(!menu->on_close());menu->render();assert(screen[8].find("FAILED")!=std::string::npos&&config==requested);
    assert(menu->get_options().back()==14);assert(menu->on_choose(5)&&!overlay_on());
    fake_fs::write_error=false;assert(menu->on_close());menu->render();assert(screen[8]=="SAVE: SAVED");
    ui16_crt_end_session();ui16_crt_prepare_game(1,"sd:nes/game.nes");ui16_crt_new_session();assert(config==requested);
    menu.reset(ui16_create_crt_menu(true,true));assert(!menu->on_choose(3));assert((config&UI16_CRT_MASK)==0);
    fake_fs::write_error=true;assert(!menu->on_close());
    s=load();assert(s.user_override&&s.initial.enabled); // Failed reset did not claim durable removal.
    fake_fs::write_error=false;
    assert(menu->on_close());s=load();assert(!s.user_override&&s.initial==off);
    assert(fake_fs::handles.empty()&&critical_depth==0);
    std::puts("CRT persistence PASS: hierarchy, CRC/size/system/volume identity, rename, two generations, 32 torn writes, corrupt/future records, I/O failures, tombstone recommendation updates, real popup save/reset/retry/leave-unsaved");
}
