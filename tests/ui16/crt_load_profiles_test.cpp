#include "ui16_crt.h"
#include "ui16_crt_save.h"
#include "ui16_crt_defaults_reader.h"
#include "ui16_osd.h"
#include "menu_manager.h"
#include <cassert>
#include <array>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

static std::string directory;
static std::string bytes;
static bool active=false;
static unsigned mutation_attempts=0;
static FIL file;
alignas(64) static std::array<unsigned char,8192> buffer;
static std::uint32_t config=0;
void enter_critical(){} void leave_critical(){}
std::uint32_t get_core_config(){return config;}
void set_core_config(std::uint32_t v){config=v;}
std::uint64_t bflb_mtimer_get_time_ms(){return 0;}
void fpga_tx_header(int,int){}
void overlay_clear(){ui16_osd_capture_clear();}
void overlay_cursor(int x,int y){ui16_osd_capture_cursor(x,y);}
void overlay_printf(const char *format,...){
    char s[256];va_list args;va_start(args,format);std::vsnprintf(s,sizeof(s),format,args);va_end(args);ui16_osd_capture_text(s);
}
extern "C" FRESULT f_open(FIL *f,const char *path,unsigned char mode){
    assert(f==&file&&!active);
    if(mode!=FA_READ){++mutation_attempts;return FR_DISK_ERR;}
    assert(std::string(path).find("sd:/")==0);
    std::ifstream in(directory+'/'+std::string(path+4),std::ios::binary);
    if(!in)return FR_NO_FILE;
    bytes.assign(std::istreambuf_iterator<char>(in),{});
    active=true;f->size=bytes.size();return FR_OK;
}
extern "C" FRESULT f_read(FIL *f,void *out,UINT wanted,UINT *count){
    assert(f==&file&&active&&out==buffer.data());
    *count=static_cast<UINT>(std::min<std::size_t>(wanted,bytes.size()));
    std::memcpy(out,bytes.data(),*count);return FR_OK;
}
extern "C" FRESULT f_close(FIL *f){assert(f==&file&&active);active=false;return FR_OK;}
extern "C" FRESULT f_write(FIL *,const void *,UINT,UINT *){++mutation_attempts;return FR_DISK_ERR;}
extern "C" FRESULT f_sync(FIL *){++mutation_attempts;return FR_DISK_ERR;}
extern "C" FRESULT f_mkdir(const char *){++mutation_attempts;return FR_DISK_ERR;}
static bool save(const Ui16RomIdentity &id,const Ui16CrtSettings &value,bool reset){
    return ui16_save_crt_override(id,value,file,buffer.data(),buffer.size(),reset);
}
static void check(Menu &menu,int row,const std::string &expected){
    ui16_osd_capture_begin();menu.render();ui16_osd_capture_end();
    assert(std::string(ui16_osd_canvas().cells.data()+row*32,32).find(expected)!=std::string::npos);
}
int main(int argc,char **argv){
    assert(argc==2);
    const Ui16RomIdentity id{true,Ui16RomVolume::Sd,2,0x232bb5b8,2097152};
    for(unsigned profile=0;profile<3;++profile){
        directory=std::string(argv[1])+'/'+(profile==0?"system":profile==1?"global":"errors");
        const auto restored=ui16_read_crt_override(id,file,buffer.data(),buffer.size());
        const auto defaults=ui16_read_crt_defaults(2,"sd:snes/game.sfc",file,buffer.data(),buffer.size());
        assert(restored.state==(profile==0?Ui16CrtReadState::Recovered:profile==1?Ui16CrtReadState::Bad:Ui16CrtReadState::Future));
        assert(defaults.state==(profile<2?Ui16CrtDefaultsState::Ready:Ui16CrtDefaultsState::Bad));
        ui16_crt_snes_session({},id,restored,save,defaults);
        const Ui16CrtSettings expected=profile==0?Ui16CrtSettings{true,2,1}:profile==1?Ui16CrtSettings{true,1,3}:Ui16CrtSettings{};
        assert(config==expected.merge(0));
        std::unique_ptr<Menu> menu(ui16_create_crt_menu(true,true));assert(menu->on_info());
        check(*menu,6,profile==0?"LOADED FROM: SYSTEM":profile==1?"LOADED FROM: GLOBAL":"LOADED FROM: BUILT-IN");
        check(*menu,7,profile==0?"SAVE READ: RECOVERED":profile==1?"SAVE READ: BAD SAVE":"SAVE READ: NEW SCHEMA");
        check(*menu,10,profile<2?"DEFAULTS: OK":"DEFAULTS: BAD FILE");
        check(*menu,5,profile==0?"SAVE: SAVED":"SAVE: RECOMMENDED");
        if(profile==2){
            assert(menu->on_adjust(0,1));assert(!menu->on_choose(3));
            check(*menu,3,"SAVE FAILED");check(*menu,5,"SAVE: FAILED");
            check(*menu,6,"LOADED FROM: BUILT-IN");check(*menu,7,"SAVE READ: NEW SCHEMA");
            assert(menu->on_close()&&config==0);
            assert(!menu->on_choose(4)&&!menu->on_choose(3));check(*menu,5,"SAVE: FAILED");
            assert(menu->on_close()&&config==0);
        }
        assert(!active&&mutation_attempts==0);
    }
    std::puts("Load fixtures PASS: SYSTEM/RECOVERED, GLOBAL/BAD SAVE, BUILT-IN/BAD FILE/NEW SCHEMA, USER+Reset SAVE refused before any mutation");
}
