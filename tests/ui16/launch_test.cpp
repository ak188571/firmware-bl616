#include "ui16_browser.h"
#include "ui16_launch.h"
#include "ui16_titles.h"
#include "utils.h"
extern "C" {
#include "ff.h"
}
#include <cassert>
#include <cstdio>
#include <map>

extern "C" FRESULT f_opendir(DIR *,const char *){return FR_NO_PATH;}
extern "C" FRESULT f_readdir(DIR *,FILINFO *){return FR_DISK_ERR;}
extern "C" FRESULT f_closedir(DIR *){return FR_OK;}
extern "C" FRESULT f_open(FIL *,const char *,unsigned char){return FR_NO_FILE;}
extern "C" FRESULT f_read(FIL *,void *,UINT,UINT *){return FR_DISK_ERR;}
extern "C" FRESULT f_close(FIL *){return FR_OK;}
int critical_depth=0;
void enter_critical(){++critical_depth;}
void leave_critical(){--critical_depth;}
void fpga_tx_byte(std::uint8_t){}
void fpga_tx_header(int,int){}
struct Fake {
    std::vector<std::string> calls;
    bool rom=true,core=true,monitor=true,program=true,wait=true,load=true,owner=true,capable=true,notice=true;
    static Fake &get(void *p){return *static_cast<Fake*>(p);}
    static bool file(const char *p,void *v){auto &f=get(v);f.calls.push_back("stat:"+std::string(p));return f.rom;}
    static bool find(const char *n,std::string &p,void *v){auto &f=get(v);f.calls.push_back("find:"+std::string(n));p="sd:cores/console138k/"+std::string(n);return std::string(n)=="monitor.bin"?f.monitor:f.core;}
    static bool prog(const char *p,void *v){auto &f=get(v);f.calls.push_back("program:"+std::string(p));return f.program;}
    static bool probe(int id,void *v){auto &f=get(v);f.calls.push_back("wait:"+std::to_string(id));return f.wait;}
    static bool romload(int id,const char *p,void *v){auto &f=get(v);f.calls.push_back("load:"+std::to_string(id)+":"+p);return f.load;}
    static void display(bool owner,void *v){auto &f=get(v);f.calls.push_back(owner?"ui16":"legacy");f.owner=owner;}
    static bool caps(void *v){return get(v).capable;}
    static bool starting(void *v){auto &f=get(v);assert(f.owner);f.calls.push_back("starting");return f.notice;}
    Ui16LaunchOps ops(){return {this,file,find,prog,probe,romload,display,caps,starting};}
};
static Ui16GameList data;
static Ui16GameList loader(int,const char *path,void *){
    if(!*path)return data;
    Ui16GameList g;g.status=Ui16ListStatus::Ready;g.names={"Real_Name.NES"};g.titles={u8"日本\nゲーム"};g.directories={false};g.crt_recommendations={{true,{true,1,2}}};return g;
}
int main(){
    const Ui16LaunchTarget target{1,"nes","nestang.bin"};
    const Ui16LaunchRequest req{1,"action/nested","Real_Name.NES"};
    Fake f;Ui16Runtime runtime(f.ops());assert(runtime.launch("sd:",target,req)==Ui16LaunchError::None);
    assert(!runtime.monitor_ready()&&!f.owner);
    assert((f.calls==std::vector<std::string>{"stat:sd:nes/action/nested/Real_Name.NES","find:nestang.bin","find:monitor.bin","starting","legacy","program:sd:cores/console138k/nestang.bin","wait:1","load:1:sd:nes/action/nested/Real_Name.NES"}));
    assert(runtime.restore_monitor()==Ui16LaunchError::None&&runtime.monitor_ready()&&f.owner);
    assert(f.calls[f.calls.size()-2]=="wait:0"&&f.calls.back()=="ui16");
    const auto n=f.calls.size();assert(runtime.restore_monitor()==Ui16LaunchError::None&&f.calls.size()==n);
    // All hardware-changing failures require monitor restoration; retries remain possible.
    for(auto expected:{Ui16LaunchError::ProgramFailed,Ui16LaunchError::CoreTimeout,Ui16LaunchError::RomFailed}){
        Fake bad;bad.program=expected!=Ui16LaunchError::ProgramFailed;bad.wait=expected!=Ui16LaunchError::CoreTimeout;bad.load=expected!=Ui16LaunchError::RomFailed;
        Ui16Runtime r(bad.ops());assert(r.launch("usb:",target,req)==expected&&!r.monitor_ready());
        if(expected!=Ui16LaunchError::RomFailed)for(auto &c:bad.calls)assert(c.find("load:")!=0);
        bad.program=bad.wait=bad.load=true;assert(r.restore_monitor()==Ui16LaunchError::None&&bad.owner);
        assert(r.launch("usb:",target,req)==Ui16LaunchError::None);
        assert(bad.calls.back()=="load:1:usb:nes/action/nested/Real_Name.NES");
    }
    for(auto expected:{Ui16LaunchError::MissingRom,Ui16LaunchError::MissingCore,Ui16LaunchError::MissingMonitor}){
        Fake bad;bad.rom=expected!=Ui16LaunchError::MissingRom;bad.core=expected!=Ui16LaunchError::MissingCore;bad.monitor=expected!=Ui16LaunchError::MissingMonitor;
        Ui16Runtime r(bad.ops());assert(r.launch("sd:",target,req)==expected&&r.monitor_ready()&&bad.owner);
        for(auto &c:bad.calls)assert(c!="starting"&&c!="legacy"&&c.find("program:")!=0&&c.find("load:")!=0);
    }
    // A failed notice must leave the monitor usable and never start reconfiguration.
    Fake notice;notice.notice=false;Ui16Runtime notice_runtime(notice.ops());
    assert(notice_runtime.launch("sd:",target,req)==Ui16LaunchError::StartingDisplayFailed);
    assert(notice_runtime.monitor_ready()&&notice.owner);
    assert((notice.calls==std::vector<std::string>{"stat:sd:nes/action/nested/Real_Name.NES","find:nestang.bin","find:monitor.bin","starting"}));
    const auto notice_calls=notice.calls.size();
    assert(notice_runtime.restore_monitor()==Ui16LaunchError::None&&notice.calls.size()==notice_calls);
    notice.notice=true;assert(notice_runtime.launch("sd:",target,req)==Ui16LaunchError::None);
    assert(std::string(ui16_launch_error_text(Ui16LaunchError::StartingDisplayFailed))=="STARTING DISPLAY ERROR");
    // Existing callers may omit the optional notice callback.
    Fake compatible;
    Ui16LaunchOps compatible_ops{&compatible,Fake::file,Fake::find,Fake::prog,Fake::probe,Fake::romload,Fake::display,Fake::caps};
    Ui16Runtime compatible_runtime(compatible_ops);
    assert(compatible_runtime.launch("sd:",target,req)==Ui16LaunchError::None);
    for(const auto &call:compatible.calls)assert(call!="starting");
    Fake recover;Ui16Runtime rr(recover.ops());assert(rr.launch("sd:",target,req)==Ui16LaunchError::None);
    recover.monitor=false;assert(rr.restore_monitor()==Ui16LaunchError::MissingMonitor&&!rr.monitor_ready());
    recover.monitor=true;recover.program=false;assert(rr.restore_monitor()==Ui16LaunchError::MonitorProgramFailed);
    recover.program=true;recover.wait=false;assert(rr.restore_monitor()==Ui16LaunchError::MonitorTimeout&&!recover.owner);
    recover.wait=true;assert(rr.restore_monitor()==Ui16LaunchError::None&&recover.owner);
    for(const auto &bad:std::vector<Ui16LaunchRequest>{{2,"","x.nes"},{1,"../snes","x.nes"},{1,"action/","x.nes"},{1,"action","../x.nes"},{1,"","x.sfc"},{1,"","日本.nes"}}){
        Fake fake;Ui16Runtime r(fake.ops());assert(r.launch("sd:",target,bad)==Ui16LaunchError::InvalidSelection&&fake.calls.empty());
    }
    Fake old;old.capable=false;Ui16Runtime old_runtime(old.ops());
    assert(old_runtime.launch("sd:",target,req)==Ui16LaunchError::OsdUnsupported);
    for(const auto &c:old.calls)assert(c.find("load:")!=0);
    assert(old_runtime.restore_monitor()==Ui16LaunchError::None);
    // Real browser emits each request once, retaining the mixed-height menu state.
    data={};data.status=Ui16ListStatus::Ready;
    for(int i=0;i<40;++i){data.names.push_back(i==25?"action":"g"+std::to_string(i)+".nes");data.titles.push_back(i%2?u8"日本\nゲーム":"SHORT");data.directories.push_back(i==25);}
    const Ui16MenuEntry entries[]={{1,"NES"}};Ui16Browser browser(entries,1);unsigned time=0;
    auto tap=[&](unsigned key){browser.update(0,time+=10,loader);return browser.update(key,time+=10,loader);};
    Ui16LaunchRequest request;
    tap(0x0100);assert(!browser.take_launch_request(request));
    for(int i=0;i<25;++i)tap(UI16_BUTTON_DOWN);
    const int top=browser.file_top();assert(top>0);
    tap(0x0001);assert(browser.relative_path()=="action"&&!browser.take_launch_request(request));
    tap(0x0100|UI16_BUTTON_DOWN);assert(browser.take_launch_request(request)&&request.relative_path=="action"&&request.filename=="Real_Name.NES");
    assert(request.crt_recommendation.present&&request.crt_recommendation.settings==Ui16CrtSettings({true,1,2}));
    assert(!browser.take_launch_request(request));
    browser.update(0x0101,time+=1000,loader);assert(!browser.take_launch_request(request));
    browser.resume_input(0x0100);browser.update(0x0100,time+=1000,loader);assert(!browser.take_launch_request(request));
    browser.resume_input(0);assert(browser.relative_path()=="action"&&browser.selected_file()==0);
    browser.set_launch_error("ROM LOAD ERROR");assert(browser.game_list().launch_error);
    tap(0x0001);assert(browser.take_launch_request(request)&&!browser.game_list().launch_error);
    tap(0x0200);assert(browser.relative_path().empty()&&browser.selected_file()==25&&browser.file_top()==top);
    // Both action groups cancel, including when sourced by separate pads and OR-merged.
    tap(0x0102);assert(!browser.take_launch_request(request));
    tap(0x0200);assert(!browser.in_games());
    assert(std::string(ui16_launch_error_text(Ui16LaunchError::RomFailed))=="ROM LOAD ERROR");
    // Real CSV ordering -> browser selection -> value-owned request snapshot.
    data={};data.status=Ui16ListStatus::Ready;data.names={"b.nes","dr.nes","off.nes"};data.directories={false,false,false};
    const std::string csv="name\ttitle\tcrt_v1\ndr.nes\tDR\t1:1:2\nb.nes\tB\t\noff.nes\tOFF\t0:0:0\n";
    Ui16TitlesParser parser(data);assert(parser.feed(csv.data(),csv.size())&&parser.finish());
    Ui16Browser presets(entries,1);unsigned clock=0;
    auto choose=[&](unsigned key){presets.update(0,clock+=10,loader);return presets.update(key,clock+=10,loader);};
    choose(0x0100);choose(0x0100);Ui16LaunchRequest dr;
    assert(presets.take_launch_request(dr)&&dr.filename=="dr.nes");
    assert(dr.crt_recommendation.present&&dr.crt_recommendation.settings==Ui16CrtSettings({true,1,2}));
    choose(UI16_BUTTON_DOWN);choose(0x0100);assert(presets.take_launch_request(request)&&request.filename=="b.nes"&&!request.crt_recommendation.present);
    choose(UI16_BUTTON_DOWN);choose(0x0100);assert(presets.take_launch_request(request)&&request.filename=="off.nes"&&request.crt_recommendation.present&&!request.crt_recommendation.settings.enabled);
    assert(dr.filename=="dr.nes"&&dr.crt_recommendation.settings==Ui16CrtSettings({true,1,2}));
    for(unsigned invalid=0;invalid<3;++invalid){
        data.names={"only.nes"};data.directories={false};data.crt_recommendations={{true,{true,1,2}}};data.titles_error=invalid==0;
        if(invalid==1)data.crt_recommendations.clear();
        if(invalid==2)data.crt_recommendations.push_back({true,{true,3,3}});
        Ui16Browser fallback(entries,1);fallback.update(0x0100,0,loader);fallback.update(0,10,loader);fallback.update(0x0100,20,loader);
        assert(fallback.take_launch_request(request)&&!request.crt_recommendation.present);
    }
    std::puts("Launch tests passed: original paths, program/ID/load failures and recovery, CSV ordering/selected and nested preset snapshots, absent/invalid-list fallback, mixed-height selection restore");
}
