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
static void expect(unsigned flags,std::array<std::uint8_t,4> widths={}){
    assert(config==((std::uint32_t(flags)<<16)|0x0123u) && transmitted==widths);
}
static void change(Menu &m,int row,int delta=1){assert(m.on_adjust(row,delta));}
static void choose(Menu &m,int row){assert(!m.on_choose(row));}

int main(int argc,char **argv){
    assert(argc==2);
    const unsigned system=unsigned(argv[1][0]-'0');
    const auto a=ui16_identify_rom(system,"sd:/a.rom",file,buffer.data(),buffer.size());
    const auto b=ui16_identify_rom(system,"sd:/b.rom",file,buffer.data(),buffer.size());
    assert(a.valid&&b.valid&&!a.same_game(b));
    ui16_crt_set_width_sender(ui16_send_crt_widths);
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
    choose(*menu,0);line(*menu,4,"SAVE *");choose(*menu,4);line(*menu,4,"SAVE OK");
    menu=launch(b);expect(0);
    change(*menu,1);choose(*menu,0);change(*menu,3,-1);choose(*menu,0);
    change(*menu,5,-1);choose(*menu,0);choose(*menu,4);
    const std::array<std::uint8_t,4> bwidths{0,0,0,255};
    expect(0x201,bwidths); // Width saved even while Overscan is OFF.
    menu=launch(a);expect(flags,widths);choose(*menu,0);line(*menu,2,"SHADOW MASK");
    choose(*menu,0);line(*menu,2,"TOP 255 PX");change(*menu,2);assert(menu->on_close());expect(flags,widths);
    menu=launch(b);expect(0x201,bwidths);
    choose(*menu,5);expect(0);assert(menu->on_close());expect(0x201,bwidths);
    choose(*menu,5);choose(*menu,4);menu=launch(b);expect(0);
    menu=launch(a);expect(flags,widths);
    // Simulate a torn upgrade: damaged newer slot must recover the untouched v1.
    char slot[80];std::snprintf(slot,sizeof slot,"crt/%02x-%08lx-%016llx.b",system,
        static_cast<unsigned long>(a.crc),static_cast<unsigned long long>(a.size));
    fs::resize_file(slot,11);
    menu=launch(a);expect(19);
    const auto recovered=ui16_read_crt_override(a,file,buffer.data(),buffer.size());
    assert(recovered.state==Ui16CrtReadState::Recovered && recovered.generation==7);
    std::printf("PASS system %u: OSD2 controls, UART widths, v1 migration/recovery, ROM A/B reload, cancel/reset\n",system);
}
