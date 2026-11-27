#include "ui16_crt.h"
#include "ui16_crt_save.h"
#include "ui16_crt_defaults_reader.h"
#include "ui16_rom_id_reader.h"
#include "ui16_crt_widths.h"
#include "ui16_osd.h"
#include "menu_manager.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>

// Use real files in the runner's temporary directory behind the FatFs interface.
namespace fs=std::filesystem;
static FILE *stream=nullptr;
static fs::path local(const char *p){
    assert(std::string(p).rfind("sd:/",0)==0);
    return fs::path(p+4);
}
extern "C" FRESULT f_open(FIL *f,const char *p,unsigned char mode){
    assert(!stream);
    stream=std::fopen(local(p).string().c_str(),mode==FA_READ?"rb":"wb");
    if(!stream)return FR_NO_FILE;
    f->size=fs::file_size(local(p));
    return FR_OK;
}
extern "C" FRESULT f_close(FIL *){int rc=std::fclose(stream);stream=nullptr;return rc?FR_DISK_ERR:FR_OK;}
extern "C" FRESULT f_read(FIL *,void *b,UINT n,UINT *got){*got=std::fread(b,1,n,stream);return std::ferror(stream)?FR_DISK_ERR:FR_OK;}
extern "C" FRESULT f_write(FIL *,const void *b,UINT n,UINT *got){*got=std::fwrite(b,1,n,stream);return *got==n?FR_OK:FR_DISK_ERR;}
extern "C" FRESULT f_sync(FIL *){return std::fflush(stream)?FR_DISK_ERR:FR_OK;}
extern "C" FRESULT f_mkdir(const char *p){return fs::create_directory(local(p))?FR_OK:FR_EXIST;}

static FIL file;
alignas(64) static std::array<unsigned char,8192> buffer;
static std::uint32_t config=0xfedc0123;
static std::array<std::uint8_t,4> transmitted;
static unsigned byte_index=4;
int critical_depth=0;
void enter_critical(){++critical_depth;} void leave_critical(){--critical_depth;}
void fpga_tx_header(int c,int n){assert(critical_depth && c==0x14 && n==5 && byte_index==4);byte_index=0;}
void fpga_tx_byte(std::uint8_t b){assert(critical_depth && byte_index<4);transmitted[byte_index++]=b;}
std::uint32_t get_core_config(){return config;}
void set_core_config(std::uint32_t v){assert(byte_index==4 && !critical_depth);config=v;}
std::uint64_t bflb_mtimer_get_time_ms(){return 0;}
void overlay_clear(){ui16_osd_capture_clear();}
void overlay_cursor(int x,int y){ui16_osd_capture_cursor(x,y);}
void overlay_printf(const char *fmt,...){
    char s[64];va_list a;va_start(a,fmt);std::vsnprintf(s,sizeof s,fmt,a);va_end(a);ui16_osd_capture_text(s);
}
static bool save(const Ui16RomIdentity &id,const Ui16CrtSettings &s,bool reset){
    return ui16_save_crt_override(id,s,file,buffer.data(),buffer.size(),reset);
}
static std::unique_ptr<Menu> launch(const Ui16RomIdentity &id,const Ui16CrtRecommendation &csv={}){
    const auto data=ui16_read_crt_override(id,file,buffer.data(),buffer.size());
    const auto defaults=ui16_read_crt_defaults(id.system,"sd:/a.rom",file,buffer.data(),buffer.size());
    if(id.system==1)ui16_crt_new_session(csv,id,data,save,defaults);
    else ui16_crt_snes_session(csv,id,data,save,defaults);
    return std::unique_ptr<Menu>(ui16_create_crt_menu(true,true));
}
static void line(Menu &m,int row,const char *value){
    ui16_osd_capture_begin();m.render();ui16_osd_capture_end();
    const auto &cells=ui16_osd_canvas().cells;
    assert(std::string(cells.data()+row*32,32).find(value)!=std::string::npos);
}
static void expect(unsigned flags,std::array<std::uint8_t,4> widths={},bool glow=false){
    assert(config==((std::uint32_t(flags)<<16)|0x0123u|(glow?0x8000u:0u)) && transmitted==widths);
}
static void change(Menu &m,int row,int delta=1){assert(m.on_adjust(row,delta));}
static void choose(Menu &m,int row){assert(!m.on_choose(row));}

int main(int argc,char **argv){
    assert(argc==2);
    const unsigned system=unsigned(argv[1][0]-'0');
    const auto a=ui16_identify_rom(system,"sd:/a.rom",file,buffer.data(),buffer.size());
    const auto b=ui16_identify_rom(system,"sd:/b.rom",file,buffer.data(),buffer.size());
    assert(a.valid&&b.valid&&!a.same_game(b));
    // An older core owns its low half-word. It cannot select the new row.
    config |= Ui16CrtSettings::GlowBit;
    ui16_crt_set_width_sender(ui16_send_crt_widths);
    auto old_menu=launch(a);choose(*old_menu,0);
    assert(old_menu->get_options().back()==7 && !old_menu->on_adjust(8,1));
    assert(config & Ui16CrtSettings::GlowBit);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true);
    auto menu=launch(a);
    expect(19); // Schema 1, master ON, BLUR WEAK, SCAN STD.
    choose(*menu,0);line(*menu,0,"EFFECTS");
    unsigned flags=19;
    for(auto pair:std::array<std::array<unsigned,2>,7>{{{1,5},{2,6},{3,8},{4,10},{5,12},{6,13},{7,15}}}){
        const auto row=pair[0],bit=pair[1];
        change(*menu,row);flags|=1u<<bit;expect(flags);
        if(row>=2 && row<=4){
            change(*menu,row);flags+=1u<<bit;expect(flags);
            change(*menu,row,-1);flags-=1u<<bit;expect(flags);
        }
    }
    line(*menu,2,"MASK GRILLE");change(*menu,2);flags+=1u<<6;line(*menu,2,"MASK SHADOW");
    choose(*menu,0);line(*menu,0,"OVERSCAN");change(*menu,1);flags|=1u<<14;expect(flags);
    std::array<std::uint8_t,4> widths{};
    for(int i=0;i<4;++i){
        for(int n=0;n<=i;++n){change(*menu,i+2);++widths[i];expect(flags,widths);}
    }
    // Visit both byte limits without disturbing another edge.
    change(*menu,2,-1);change(*menu,2,-1);widths[0]=255;expect(flags,widths);
    choose(*menu,0);choose(*menu,0);line(*menu,8,"GLOW OFF");
    change(*menu,8);line(*menu,8,"GLOW ON");expect(flags,widths,true);
    assert(menu->get_options().back()==8);
    menu->on_info(0x0008);
    ui16_crt_accept_status(config ^ Ui16CrtSettings::GlowBit);
    menu->on_tick(1);line(*menu,3,"WAITING");
    ui16_crt_accept_status(config);
    menu->on_tick(2);line(*menu,3,"FRAME ACK");
    menu->on_info(0x0008);
    change(*menu,8,-1);line(*menu,8,"GLOW OFF");expect(flags,widths);
    change(*menu,8);expect(flags,widths,true);
    choose(*menu,0);choose(*menu,0);line(*menu,4,"SAVE *");choose(*menu,4);line(*menu,4,"SAVE OK");
    menu=launch(b);expect(0);
    change(*menu,1);choose(*menu,0);change(*menu,3,-1);choose(*menu,0);
    change(*menu,5,-1);choose(*menu,0);choose(*menu,4);
    const std::array<std::uint8_t,4> bwidths{0,0,0,255};
    expect(0x201,bwidths); // Width saved even while Overscan is OFF.
    menu=launch(a);expect(flags,widths,true);choose(*menu,0);line(*menu,2,"MASK SHADOW");
    choose(*menu,0);line(*menu,2,"TOP   :255PX");change(*menu,2);assert(menu->on_close());expect(flags,widths,true);
    menu=launch(b);expect(0x201,bwidths);
    choose(*menu,5);expect(0);assert(menu->on_close());expect(0x201,bwidths);
    choose(*menu,5);choose(*menu,4);menu=launch(b);expect(0);
    menu=launch(a);expect(flags,widths,true);
    // Simulate a torn upgrade: damaged newer slot must recover the untouched v1.
    char slot[80];std::snprintf(slot,sizeof slot,"crt/%02x-%08lx-%016llx.b",system,
        static_cast<unsigned long>(a.crc),static_cast<unsigned long long>(a.size));
    fs::resize_file(slot,11);
    menu=launch(a);expect(19);
    const auto recovered=ui16_read_crt_override(a,file,buffer.data(),buffer.size());
    assert(recovered.state==Ui16CrtReadState::Recovered && recovered.generation==7);
    // The signal selector is the last selectable option on MAIN. Older records
    // supply the RGB bank verbatim. Changing either bank must preserve the other.
    line(*menu,6,"SIGNAL RGB");change(*menu,6);expect(0x8013);
    line(*menu,6,"SIGNAL COMP.");
    menu->on_info(0x0008);
    ui16_crt_accept_status(config ^ (1u<<31));menu->on_tick(3);line(*menu,3,"WAITING");
    ui16_crt_accept_status(config);menu->on_tick(4);line(*menu,3,"FRAME ACK");
    menu->on_info(0x0008);assert(menu->on_close());expect(19);
    menu=launch(b);change(*menu,1);expect(1);
    choose(*menu,6);line(*menu,2,"BLUR WEAK");expect(0x8003);
    change(*menu,2);expect(0x8005); // Composite STD.
    choose(*menu,0);change(*menu,7);expect(5); // Composite bleed OFF.
    change(*menu,8);expect(5,{},true);       // Glow is shared.
    choose(*menu,0);choose(*menu,0);choose(*menu,4);line(*menu,4,"SAVE OK");
    menu=launch(a);expect(19);line(*menu,6,"SIGNAL RGB");
    menu=launch(b);expect(5,{},true);line(*menu,6,"SIGNAL COMP.");
    change(*menu,6,-1);expect(1,{},true);line(*menu,2,"BLUR OFF");
    change(*menu,2,-1);expect(7,{},true); // RGB STRONG.
    choose(*menu,0);change(*menu,7);expect(0x8007,{},true);
    choose(*menu,0);choose(*menu,0);choose(*menu,4);
    menu=launch(b);expect(0x8007,{},true);line(*menu,6,"SIGNAL RGB");
    choose(*menu,6);expect(5,{},true);
    choose(*menu,0);line(*menu,7,"BLEED OFF");
    assert(menu->on_close());expect(0x8007,{},true);
    menu=launch(b);choose(*menu,5);expect(0);choose(*menu,4);
    menu=launch(b);line(*menu,6,"SIGNAL RGB");expect(0);choose(*menu,6);expect(0x8002);
    ui16_crt_preview_session();
    menu.reset(ui16_create_crt_menu(true,true));
    assert(menu->get_options().back()==6 && menu->get_options().size()==5);
    choose(*menu,4);line(*menu,6,"SIGNAL COMP.");
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true);
    menu=launch(a);choose(*menu,0);line(*menu,9,"CURVE OFF");expect(19);
    change(*menu,9);line(*menu,9,"CURVE ON");
    assert(config==(19u<<16 | Ui16CrtSettings::CurveBit | 0x123u));
    assert(menu->on_close());expect(19);
    menu=launch(b);choose(*menu,1);choose(*menu,6);choose(*menu,0);
    change(*menu,8);change(*menu,9);
    const auto curved=config;assert((curved & 0xc000u)==0xc000u);
    menu->on_info(0x0008);ui16_crt_accept_status(curved^Ui16CrtSettings::CurveBit);
    menu->on_tick(10);line(*menu,3,"WAITING");
    ui16_crt_accept_status(curved);menu->on_tick(11);line(*menu,3,"FRAME ACK");
    menu->on_info(0x0008);choose(*menu,0);choose(*menu,0);
    choose(*menu,4);line(*menu,4,"SAVE OK");
    menu=launch(a);expect(19);menu=launch(b);assert(config==curved);
    line(*menu,6,"SIGNAL COMP.");choose(*menu,0);line(*menu,9,"CURVE ON");
    change(*menu,9);assert(menu->on_close() && config==curved);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,false);
    config&=~Ui16CrtSettings::CurveBit;menu=launch(b);
    assert(!(config & Ui16CrtSettings::CurveBit));choose(*menu,0);
    assert(menu->get_options().back()==8 && !menu->on_adjust(9,1));
    config|=Ui16CrtSettings::CurveBit;menu=launch(a);
    assert(config & Ui16CrtSettings::CurveBit); // Old core owns this bit.
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true);
    menu=launch(b);choose(*menu,5);choose(*menu,4);
    menu=launch(b);expect(0);choose(*menu,0);line(*menu,9,"CURVE OFF");
    std::printf("PASS system %u: curvature capability, frame ACK, ROM-specific schema 6, cancel/reset and old-core bit ownership\n",system);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true);
    menu=launch(b);choose(*menu,0);line(*menu,10,"CONVERG. OFF");
    change(*menu,10);assert(config & Ui16CrtSettings::ConvergenceBit);
    assert(menu->on_close());expect(0);
    menu=launch(b);change(*menu,1);choose(*menu,0);change(*menu,10);
    const auto converged=config;
    menu->on_info(0x0008);ui16_crt_accept_status(converged^Ui16CrtSettings::ConvergenceBit);
    menu->on_tick(20);line(*menu,3,"WAITING");
    ui16_crt_accept_status(converged);menu->on_tick(21);line(*menu,3,"FRAME ACK");
    menu->on_info(0x0008);choose(*menu,0);choose(*menu,0);choose(*menu,4);line(*menu,4,"SAVE OK");
    menu=launch(a);expect(19);menu=launch(b);assert(config==converged);
    choose(*menu,0);line(*menu,9,"CURVE OFF");line(*menu,10,"CONVERG. ON");
    change(*menu,9);choose(*menu,0);choose(*menu,0);choose(*menu,4);
    menu=launch(b);assert(config==(converged|Ui16CrtSettings::CurveBit));
    choose(*menu,0);change(*menu,10);choose(*menu,0);choose(*menu,0);choose(*menu,4);
    menu=launch(b);choose(*menu,0);line(*menu,9,"CURVE ON");line(*menu,10,"CONVERG. OFF");
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true);
    config|=Ui16CrtSettings::ConvergenceBit;menu=launch(a);choose(*menu,0);
    assert((config & Ui16CrtSettings::ConvergenceBit) && menu->get_options().back()==9 && !menu->on_adjust(10,1));
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true);
    menu=launch(b);choose(*menu,5);choose(*menu,4);menu=launch(b);expect(0);
    std::printf("PASS system %u: convergence row, frame ACK, ROM-specific save with curvature OFF/ON, legacy restore, cancel/reset and previous-core gating\n",system);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true);
    menu=launch(b);choose(*menu,1);line(*menu,6,"SIGNAL RGB");
    change(*menu,6);line(*menu,6,"SIGNAL COMP.");assert(!(config & Ui16CrtSettings::NtscBit));
    change(*menu,6);line(*menu,6,"SIGNAL NTSC");assert(config & Ui16CrtSettings::NtscBit);
    menu->on_info(0x0008);
    ui16_crt_accept_status(config ^ Ui16CrtSettings::NtscBit);menu->on_tick(30);line(*menu,3,"WAITING");
    ui16_crt_accept_status(config);menu->on_tick(31);line(*menu,3,"FRAME ACK");
    menu->on_info(0x0008);
    change(*menu,6,-1);line(*menu,6,"SIGNAL COMP.");
    change(*menu,6,-1);line(*menu,6,"SIGNAL RGB");
    change(*menu,6,-1);line(*menu,6,"SIGNAL NTSC");
    assert(menu->on_close());expect(0);
    menu=launch(b);choose(*menu,1);change(*menu,6,-1);
    choose(*menu,4);line(*menu,4,"SAVE OK");
    const auto saved_ntsc=config;
    menu=launch(a);expect(19);menu=launch(b);assert(config==saved_ntsc);
    line(*menu,6,"SIGNAL NTSC");choose(*menu,0);change(*menu,9);change(*menu,10);
    choose(*menu,0);choose(*menu,0);choose(*menu,4);
    menu=launch(b);assert(config==(saved_ntsc|Ui16CrtSettings::CurveBit|Ui16CrtSettings::ConvergenceBit));
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true);
    config&=~Ui16CrtSettings::NtscBit;menu=launch(b);line(*menu,6,"SIGNAL COMP.");
    assert(!(config & Ui16CrtSettings::NtscBit));change(*menu,6);line(*menu,6,"SIGNAL RGB");
    config|=Ui16CrtSettings::NtscBit;menu=launch(a);assert(config & Ui16CrtSettings::NtscBit);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true);
    menu=launch(b);choose(*menu,5);choose(*menu,4);menu=launch(b);expect(0);
    std::printf("PASS system %u: NTSC signal cycling, per-ROM save, previous-core gating, cancel/reset\n",system);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true,true);
    menu=launch(b);choose(*menu,1);choose(*menu,0);
    line(*menu,11,"BLOOM OFF");change(*menu,11);line(*menu,11,"BLOOM ON");
    assert(config & Ui16CrtSettings::BloomBit);
    menu->on_info(0x0008);
    ui16_crt_accept_status(config ^ Ui16CrtSettings::BloomBit);menu->on_tick(40);line(*menu,3,"WAITING");
    ui16_crt_accept_status(config);menu->on_tick(41);line(*menu,3,"FRAME ACK");
    menu->on_info(0x0008);
    assert(menu->on_close());expect(0); // unsaved preview was discarded
    menu=launch(b);choose(*menu,1);choose(*menu,0);change(*menu,11);
    change(*menu,8);choose(*menu,0);choose(*menu,0);choose(*menu,4);line(*menu,4,"SAVE OK");
    const auto saved_bloom=config;
    menu=launch(a);expect(19);menu=launch(b);assert(config==saved_bloom);
    choose(*menu,0);line(*menu,8,"GLOW ON");line(*menu,11,"BLOOM ON");
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true);
    config&=~Ui16CrtSettings::BloomBit;menu=launch(b);choose(*menu,0);
    assert(menu->get_options().back()==10 && !menu->on_adjust(11,1) && !(config & Ui16CrtSettings::BloomBit));
    config|=Ui16CrtSettings::BloomBit;menu=launch(a);assert(config & Ui16CrtSettings::BloomBit);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true,true);
    menu=launch(b);choose(*menu,5);choose(*menu,4);menu=launch(b);expect(0);
    std::printf("PASS system %u: Bloom, Glow independence, frame ACK, ROM save/restore, cancel, reset and old-core gating\n",system);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true,true,true);
    menu=launch(b);choose(*menu,1);choose(*menu,0);
    line(*menu,12,"AFTRGLW OFF");change(*menu,12);line(*menu,12,"AFTRGLW ON");
    assert(config & Ui16CrtSettings::PersistenceBit);
    menu->on_info(0x0008);
    ui16_crt_accept_status(config ^ Ui16CrtSettings::PersistenceBit);menu->on_tick(40);line(*menu,3,"WAITING");
    ui16_crt_accept_status(config);menu->on_tick(41);line(*menu,3,"FRAME ACK");
    menu->on_info(0x0008);
    assert(menu->on_close());expect(0); // unsaved preview was discarded
    menu=launch(b);choose(*menu,1);choose(*menu,0);change(*menu,12);
    change(*menu,11);choose(*menu,0);choose(*menu,0);choose(*menu,4);line(*menu,4,"SAVE OK");
    const auto saved_persistence=config;
    menu=launch(a);expect(19);menu=launch(b);assert(config==saved_persistence);
    choose(*menu,0);line(*menu,11,"BLOOM ON");line(*menu,12,"AFTRGLW ON");
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true,true);
    config&=~Ui16CrtSettings::PersistenceBit;menu=launch(b);choose(*menu,0);
    assert(menu->get_options().back()==11 && !menu->on_adjust(12,1) && !(config & Ui16CrtSettings::PersistenceBit));
    config|=Ui16CrtSettings::PersistenceBit;menu=launch(a);assert(config & Ui16CrtSettings::PersistenceBit);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true,true,true);
    menu=launch(b);choose(*menu,5);choose(*menu,4);menu=launch(b);expect(0);
    std::printf("PASS system %u: Afterglow, Bloom independence, frame ACK, ROM save/restore, cancel, reset and old-core gating\n",system);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true,true,true,true);
    menu=launch(a);expect(19);choose(*menu,0);line(*menu,13,"BEAMSPOT OFF");
    menu=launch(b);choose(*menu,1);choose(*menu,0);change(*menu,13);
    line(*menu,13,"BEAMSPOT ON");assert(config & Ui16CrtSettings::SpotBit);
    menu->on_info(0x0008);
    ui16_crt_accept_status(config ^ Ui16CrtSettings::SpotBit);menu->on_tick(42);line(*menu,3,"WAITING");
    ui16_crt_accept_status(config);menu->on_tick(43);line(*menu,3,"FRAME ACK");
    menu->on_info(0x0008);
    assert(menu->on_close());expect(0);
    menu=launch(b);choose(*menu,1);choose(*menu,0);change(*menu,13);
    choose(*menu,0);choose(*menu,0);choose(*menu,4);line(*menu,4,"SAVE OK");
    menu=launch(a);expect(19);menu=launch(b);assert(config==(0x10123u | Ui16CrtSettings::SpotBit));
    choose(*menu,0);line(*menu,13,"BEAMSPOT ON");line(*menu,1,"BEAM OFF");
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true,true,true);
    menu=launch(a);choose(*menu,0);
    assert(menu->get_options().back()==12 && !menu->on_adjust(13,1) && (config & Ui16CrtSettings::SpotBit));
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true,true,true,true);
    menu=launch(b);choose(*menu,5);choose(*menu,4);menu=launch(b);expect(0);
    std::printf("PASS system %u: beam spot, legacy OFF, frame ACK, independent ROM saves, reset/cancel and old core\n",system);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true,true,true,true,Ui16CrtSettings::DisplayRevision);
    menu=launch(a);choose(*menu,0);line(*menu,14,"PITCH 1PX");
    menu=launch(b);choose(*menu,1);choose(*menu,0);
    change(*menu,2,-1);line(*menu,2,"MASK SLOT");
    change(*menu,14);line(*menu,14,"PITCH 2PX");
    choose(*menu,0);choose(*menu,0);choose(*menu,4);line(*menu,4,"SAVE OK");
    const auto saved_display=config;
    menu=launch(a);choose(*menu,0);line(*menu,2,"MASK OFF");line(*menu,14,"PITCH 1PX");
    menu=launch(b);assert(config==saved_display);choose(*menu,0);line(*menu,2,"MASK SLOT");line(*menu,14,"PITCH 2PX");
    change(*menu,14);assert(menu->on_close());assert(config==saved_display);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true,true,true,true);
    menu=launch(b);choose(*menu,0);line(*menu,2,"MASK OFF");assert(menu->get_options().back()==13);
    std::printf("PASS system %u: slot, pitch, per-ROM restore, cancel and capability gating\n",system);
    ui16_crt_set_width_sender(ui16_send_crt_widths,true,true,true,true,true,true,true,Ui16CrtSettings::DisplayRevision);
    menu=launch(b);change(*menu,6,-1);line(*menu,6,"SIGNAL RF");
    assert((config & (Ui16CrtSettings::RfSignal|Ui16CrtSettings::NtscBit))==(Ui16CrtSettings::RfSignal|Ui16CrtSettings::NtscBit));
    choose(*menu,4);menu=launch(a);line(*menu,6,"SIGNAL RGB");
    menu=launch(b);line(*menu,6,"SIGNAL RF");change(*menu,6,-1);line(*menu,6,"SIGNAL NTSC");
    assert(!(config & Ui16CrtSettings::RfSignal));choose(*menu,4);
    std::printf("PASS system %u: RF signal, ROM-specific save and NTSC fallback\n",system);
    menu=launch(b);choose(*menu,0);change(*menu,15);line(*menu,15,"FOCUS ON");
    change(*menu,10,-1);line(*menu,10,"CONVERG. POS");choose(*menu,0);choose(*menu,0);choose(*menu,4);
    menu=launch(a);choose(*menu,0);line(*menu,15,"FOCUS OFF");line(*menu,10,"CONVERG. OFF");
    menu=launch(b);choose(*menu,0);line(*menu,15,"FOCUS ON");line(*menu,10,"CONVERG. POS");
    std::printf("PASS system %u: edge focus and positional convergence per ROM\n",system);
    menu=launch(b);choose(*menu,0);change(*menu,12,-1);line(*menu,12,"AFTRGLW RGB");
    choose(*menu,0);choose(*menu,0);choose(*menu,4);
    menu=launch(a);choose(*menu,0);line(*menu,12,"AFTRGLW OFF");
    menu=launch(b);choose(*menu,0);line(*menu,12,"AFTRGLW RGB");line(*menu,15,"FOCUS ON");
    std::printf("PASS system %u: per-primary afterglow stored independently\n",system);
    menu=launch(b);choose(*menu,0);change(*menu,16);line(*menu,16,"PHOSPHOR ON");
    choose(*menu,0);choose(*menu,0);choose(*menu,4);
    menu=launch(a);choose(*menu,0);line(*menu,16,"PHOSPHOR OFF");
    menu=launch(b);choose(*menu,0);line(*menu,16,"PHOSPHOR ON");line(*menu,12,"AFTRGLW RGB");
    menu->on_info(0x0008);ui16_crt_accept_status(config^Ui16CrtSettings::PhosphorColor);
    menu->on_tick(80);line(*menu,3,"WAITING");ui16_crt_accept_status(config);
    menu->on_tick(81);line(*menu,3,"FRAME ACK");menu->on_info(0x0008);
    choose(*menu,0);choose(*menu,0);choose(*menu,5);choose(*menu,4);
    menu=launch(b);choose(*menu,0);line(*menu,16,"PHOSPHOR OFF");line(*menu,15,"FOCUS OFF");line(*menu,14,"PITCH 1PX");
    std::printf("PASS system %u: phosphor colors, settings ACK and complete reset\n",system);
    std::printf("PASS system %u: independent RGB/Composite controls, shared Glow, legacy RGB, per-ROM v5 save/reload, cancel, reset and preview navigation\n",system);
    std::printf("PASS system %u: Glow toggle and frame ACK, old-core gating, schema 1/3/4 ROM A/B reload, cancel/reset\n",system);
    fs::copy_file("example.tsv","crt/defaults.tsv");
    menu=launch(b);
    line(*menu,6,"SIGNAL COMP.");
    line(*menu,2,"BLUR WEAK");
    const unsigned flags_global=(1u<<1)|(1u<<3)|(1u<<5)|(2u<<6)|(1u<<14);
    const auto off=(flags_global<<16)|Ui16CrtSettings::SpotBit|Ui16CrtSettings::GlowBit;
    const auto owned=0xffff0000u|0xfff8u;
    const std::array<std::uint8_t,4> crop={2,2,2,2};
    assert((config & owned)==off && transmitted==crop);
    menu->on_info(0x0008);line(*menu,1,"SRC GLOBAL");menu->on_info(0x0008);
    change(*menu,1);assert((config & owned)==(off|(1u<<16)) && transmitted==crop);
    choose(*menu,4);menu=launch(b);assert((config & owned)==(off|(1u<<16)));
    menu->on_info(0x0008);line(*menu,1,"SRC USER");menu->on_info(0x0008);
    choose(*menu,5);choose(*menu,4);
    Ui16CrtRecommendation title;assert(ui16_parse_crt("1:2:3",title));
    menu=launch(b,title);
    // CSV sets master and scan; its blur is RGB-only. Composite keeps WEAK.
    const unsigned csv_bits=Ui16CrtSettings{true,1,3}.flags()<<16;
    assert((config & owned)==((off & ~(0x1fu<<16))|csv_bits) && transmitted==crop);
    change(*menu,6,-1);line(*menu,6,"SIGNAL RGB");line(*menu,2,"BLUR STD");
    fs::remove("crt/defaults.tsv");
    std::printf("PASS system %u: GLOBAL preset, master-only toggle, four widths, ROM override, RESET and title CSV\n",system);
}
