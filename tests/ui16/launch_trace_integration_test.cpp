// Exercise the actual launch coordinator with an injected monotonic clock.
#include "ui16_launch.h"
#include <cassert>
#include <cstdio>
static std::uint64_t now=0;
std::uint64_t tc_launch_now_us(){return now;}
bool ui16_valid_component(const std::string &s){return !s.empty()&&s.find('/')==std::string::npos;}
bool ui16_is_rom_file(int,const char *){return true;}
struct Fake {
    int failure=0;
    static bool file(const char *,void *p){now+=5;return static_cast<Fake*>(p)->failure!=1;}
    static bool find(const char *,std::string &path,void *){now+=10;path="core.bin";return true;}
    static bool notice(void *){now+=500000;return true;}
    static void owner(bool,void *){}
    static bool program(const char *,void *p){now+=3000000;return static_cast<Fake*>(p)->failure!=2;}
    static bool ready(int,void *){now+=20;return true;}
    static bool osd(void *){now+=30;return true;}
    static bool load(int,const char *,void *p){now+=100;return static_cast<Fake*>(p)->failure!=3;}
};
int main(){
    for(int failure=0;failure<4;++failure){
        Fake f{failure};
        Ui16Runtime runtime({&f,Fake::file,Fake::find,Fake::program,Fake::ready,Fake::load,Fake::owner,Fake::osd,Fake::notice});
        const auto error=runtime.launch("sd:",{2,"snes","snes.bin"},{2,"","game.sfc",{}});
        assert((error==Ui16LaunchError::None)==(failure==0));
        assert(!tc_launch_record.active&&tc_launch_record.success==(failure==0));
        if(failure==0){
            assert(tc_launch_record.elapsed_us[static_cast<unsigned>(TcLaunchEvent::ProgramBegin)]==500025);
            assert(tc_launch_record.elapsed_us[static_cast<unsigned>(TcLaunchEvent::ProgramEnd)]==3500025);
            assert(tc_launch_record.elapsed_us[static_cast<unsigned>(TcLaunchEvent::Finished)]==3500175);
        }
        const auto record=tc_launch_record;
        f.failure=0;
        assert(runtime.restore_monitor()==Ui16LaunchError::None);
        assert(tc_launch_record.seen==record.seen&&tc_launch_record.origin_us==record.origin_us);
    }
    std::puts("Actual launch coordinator: success, preflight/program/ROM failure and restore preservation passed");
}
