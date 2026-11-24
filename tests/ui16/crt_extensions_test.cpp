#include "ui16_crt.h"
#include "ui16_crt_save.h"
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
alignas(64) static std::array<unsigned char,512> buffer;
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
static std::unique_ptr<Menu> launch(const Ui16RomIdentity &id){
    const auto data=ui16_read_crt_override(id,file,buffer.data(),buffer.size());
    if(id.system==1)ui16_crt_new_session({},id,data,save);
    else ui16_crt_snes_session({},id,data,save,{});
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
    line(*menu,2,"APERTURE GRILLE");change(*menu,2);flags+=1u<<6;line(*menu,2,"SHADOW MASK");
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
    menu->on_tick(1);line(*menu,12,"WAITING");
    ui16_crt_accept_status(config);
    menu->on_tick(2);line(*menu,12,"FRAME ACK");
    menu->on_info(0x0008);
    change(*menu,8,-1);line(*menu,8,"GLOW OFF");expect(flags,widths);
    change(*menu,8);expect(flags,widths,true);
    choose(*menu,0);choose(*menu,0);line(*menu,4,"SAVE *");choose(*menu,4);line(*menu,4,"SAVE OK");
    menu=launch(b);expect(0);
    change(*menu,1);choose(*menu,0);change(*menu,3,-1);choose(*menu,0);
    change(*menu,5,-1);choose(*menu,0);choose(*menu,4);
    const std::array<std::uint8_t,4> bwidths{0,0,0,255};
    expect(0x201,bwidths); // Width saved even while Overscan is OFF.
    menu=launch(a);expect(flags,widths,true);choose(*menu,0);line(*menu,2,"SHADOW MASK");
    choose(*menu,0);line(*menu,2,"TOP 255 PX");change(*menu,2);assert(menu->on_close());expect(flags,widths,true);
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
    line(*menu,6,"SIGNAL COMPOSITE");
    menu->on_info(0x0008);
    ui16_crt_accept_status(config ^ (1u<<31));menu->on_tick(3);line(*menu,10,"WAITING");
    ui16_crt_accept_status(config);menu->on_tick(4);line(*menu,10,"FRAME ACK");
    menu->on_info(0x0008);assert(menu->on_close());expect(19);
    menu=launch(b);change(*menu,1);expect(1);
    choose(*menu,6);line(*menu,2,"BLUR WEAK");expect(0x8003);
    change(*menu,2);expect(0x8005); // Composite STD.
    choose(*menu,0);change(*menu,7);expect(5); // Composite bleed OFF.
    change(*menu,8);expect(5,{},true);       // Glow is shared.
    choose(*menu,0);choose(*menu,0);choose(*menu,4);line(*menu,4,"SAVE OK");
    menu=launch(a);expect(19);line(*menu,6,"SIGNAL RGB");
    menu=launch(b);expect(5,{},true);line(*menu,6,"SIGNAL COMPOSITE");
    change(*menu,6,-1);expect(1,{},true);line(*menu,2,"BLUR OFF");
    change(*menu,2,-1);expect(7,{},true); // RGB STRONG.
    choose(*menu,0);change(*menu,7);expect(0x8007,{},true);
    choose(*menu,0);choose(*menu,0);choose(*menu,4);
    menu=launch(b);expect(0x8007,{},true);line(*menu,6,"SIGNAL RGB");
    choose(*menu,6);expect(5,{},true);
    choose(*menu,0);line(*menu,7,"CHROMA BLEED OFF");
    assert(menu->on_close());expect(0x8007,{},true);
    menu=launch(b);choose(*menu,5);expect(0);choose(*menu,4);
    menu=launch(b);line(*menu,6,"SIGNAL RGB");expect(0);choose(*menu,6);expect(0x8002);
    ui16_crt_preview_session();
    menu.reset(ui16_create_crt_menu(true,true));
    assert(menu->get_options().back()==6 && menu->get_options().size()==5);
    choose(*menu,4);line(*menu,6,"SIGNAL COMPOSITE");
    std::printf("PASS system %u: independent RGB/Composite controls, shared Glow, legacy RGB, per-ROM v5 save/reload, cancel, reset and preview navigation\n",system);
    std::printf("PASS system %u: Glow toggle and frame ACK, old-core gating, schema 1/3/4 ROM A/B reload, cancel/reset\n",system);
}
