#include "ui16_crt_defaults_reader.h"
#include <cassert>
#include <array>
#include <algorithm>
#include <string>
#include <cstring>
#include <cstdio>
static std::string bytes,path;
static FIL file;
alignas(64) static std::array<char,8192> buffer;
static unsigned opens=0,reads=0,closes=0,active=0;
static FRESULT open_result=FR_OK;
static bool read_error=false,close_error=false,early=false,oversize=false;
static std::size_t advertised=0;
static void setup(const std::string &data){
    assert(!active);bytes=data;advertised=bytes.size();opens=reads=closes=0;path.clear();
    open_result=FR_OK;read_error=close_error=early=oversize=false;
}
extern "C" FRESULT f_open(FIL *f,const char *name,unsigned char mode){
    assert(f==&file&&!active&&mode==FA_READ);++opens;path=name;
    if(open_result!=FR_OK)return open_result;
    ++active;f->position=0;f->size=advertised;return FR_OK;
}
extern "C" FRESULT f_read(FIL *f,void *out,UINT wanted,UINT *count){
    assert(f==&file&&active==1&&out==buffer.data()&&wanted==4097);++reads;*count=0;
    if(read_error)return FR_DISK_ERR;
    if(early)return FR_OK;
    if(oversize){*count=wanted+1;return FR_OK;}
    *count=static_cast<UINT>(std::min<std::size_t>(wanted,bytes.size()));
    if(*count)std::memcpy(out,bytes.data(),*count);
    return FR_OK;
}
extern "C" FRESULT f_close(FIL *f){assert(f==&file&&active==1);--active;++closes;return close_error?FR_DISK_ERR:FR_OK;}
static Ui16CrtDefaults read(int system=1,const char *rom="sd:nes/a.nes"){
    const auto result=ui16_read_crt_defaults(system,rom,file,buffer.data(),buffer.size());assert(!active);return result;
}
static void check_named(){
    const std::string header="system\tcrt_v2\n";
    const std::string fields="crt=on blur=std scan=strong beam=on mask=shadow gamma=1.25 white=warm "
        "vignette=on corners=on overscan=on top=0 bottom=1 left=0254 right=255 bleed=on "
        "glow=on signal=ntsc composite_blur=off composite_bleed=off curve=on "
        "convergence=pos bloom=on afterglow=rgb pitch=2px focus=on phosphor=on beamspot=on";
    Ui16CrtSettings all{true,2,3};
    all.beam=all.vignette=all.rounded=all.overscan=all.bleed=all.glow=true;
    all.mask=2; all.gamma=2; all.temperature=1; all.widths={0,1,254,255};
    all.composite=true; all.ntsc=true; all.composite_blur=0; all.composite_bleed=false;
    all.curvature=all.convergence=all.bloom=all.persistence=true;
    all.spot=true;
    all.display_options=Ui16CrtSettings::WidePitch | Ui16CrtSettings::EdgeFocus |
                        Ui16CrtSettings::PhosphorColor | Ui16CrtSettings::PositionRgb | Ui16CrtSettings::RgbDecay;
    for(unsigned system:{1u,2u}){
        const std::string name=system==1?"nes":"snes";
        const auto text=header+name+"\t"+fields+"\n*\ttop=42\n";
        setup(text);
        auto result=read(system);
        assert(result.named_items && result.source==Ui16CrtDefaultsSource::System);
        assert(result.recommendation.settings==all);
        result=ui16_parse_crt_defaults(text,3-system);
        Ui16CrtSettings global;global.widths[0]=42;
        assert(result.source==Ui16CrtDefaultsSource::Global && result.recommendation.settings==global);
    }
    const auto empty=ui16_parse_crt_defaults(header+"*\t\nnes\tfuture=光",1);
    const std::string rich=header+"*\tsignal=rf convergence=pos afterglow=rgb mask=slot pitch=2px focus=on phosphor=on beamspot=on\n";
    const auto full=ui16_parse_crt_defaults(rich,1).recommendation.settings;
    assert(full.valid() && full.display_options==Ui16CrtSettings::display_mask(5) && full.mask==3 && full.spot);
    const auto simpler=ui16_parse_crt_defaults(rich+"nes\tsignal=rgb convergence=on afterglow=off mask=off pitch=1px focus=off phosphor=off beamspot=off",1);
    Ui16CrtSettings plain;plain.convergence=true;
    assert(simpler.recommendation.present && simpler.recommendation.settings==plain);
    assert(empty.named_items && empty.state==Ui16CrtDefaultsState::Ready && !empty.recommendation.present);
    const auto zero=ui16_parse_crt_defaults("\xef\xbb\xbfsystem\tcrt_v2\r\n*\tbeam=on top=255\r\nnes\tbeam=off top=0",1);
    assert(zero.source==Ui16CrtDefaultsSource::System && zero.recommendation.settings==Ui16CrtSettings{});
    for(const char *row:{"unknown","crt=", "=on","mask==on","top=-1","bottom=256","crt=ON",
                         "beam=on beam=off","future=a future=b","x-y=1","1key=0",
                         "pitch=3px","convergence=rgb","afterglow=pos","signal=tv","phosphor=warm",
                         "focus=2","mask=slots","beamspot=weak","future=a\tb","future=\xc0\x80","future=a\xc2\xa0z"}){
        const auto result=ui16_parse_crt_defaults(header+"*\tbeam=on\nsnes\t"+row,1);
        assert(result.state==Ui16CrtDefaultsState::Bad && !result.recommendation.present);
    }
    assert(ui16_parse_crt_defaults(header+"nes\tcrt=on",1).state==Ui16CrtDefaultsState::Bad);
    assert(ui16_parse_crt_defaults(header+"*\t\n*\t",1).state==Ui16CrtDefaultsState::Bad);
    std::puts("PASS v2: NES/SNES fields, extended modes, scoped inheritance, explicit OFF/0, future keys and invalid rows");
}
int main(){
    check_named();
    const std::string header="system\tcrt_v1\n";
    const std::string good=header+"*\t1:1:3\nnes\t1:2:1\n";
    auto result=ui16_parse_crt_defaults(good,1);
    assert(result.state==Ui16CrtDefaultsState::Ready&&result.source==Ui16CrtDefaultsSource::System&&result.recommendation.settings==Ui16CrtSettings({true,2,1}));
    result=ui16_parse_crt_defaults(good,2);assert(result.source==Ui16CrtDefaultsSource::Global&&result.recommendation.settings==Ui16CrtSettings({true,1,3}));
    for(unsigned system=1;system<=6;++system){
        const char *const keys[]={"*","nes","snes","gba","genesis","sms","pc"};
        result=ui16_parse_crt_defaults(header+"*\t1:3:3\n"+keys[system]+"\t0:0:0\n",system);
        assert(result.source==Ui16CrtDefaultsSource::System&&result.recommendation.present&&result.recommendation.settings==Ui16CrtSettings{});
    }
    result=ui16_parse_crt_defaults(header+"*\t1:1:3\nnes\t\n",1);assert(result.source==Ui16CrtDefaultsSource::Global);
    result=ui16_parse_crt_defaults(header+"*\t0:0:0\n",255);assert(result.source==Ui16CrtDefaultsSource::Global&&result.recommendation.present&&!result.recommendation.settings.enabled);
    for(const std::string &text:{header,header+"*\t\nnes\t\n",header+"\n\n"}){
        result=ui16_parse_crt_defaults(text,1);assert(result.state==Ui16CrtDefaultsState::Ready&&!result.recommendation.present&&result.source==Ui16CrtDefaultsSource::Builtin);
    }
    result=ui16_parse_crt_defaults("\xef\xbb\xbf"+std::string("system\tcrt_v1\r\n*\t1:2:3\r\nnes\t0:0:0"),1);
    assert(result.state==Ui16CrtDefaultsState::Ready&&result.source==Ui16CrtDefaultsSource::System&&!result.recommendation.settings.enabled);
    for(const std::string &text:std::array<std::string,18>{"","\n"+header,"system\tcrt_v2\n","system,crt_v1\n",header+"unknown\t1:1:1\n",header+"NES\t1:1:1\n",header+"*\t1:1:1\n*\t\n",header+"nes\t\nnes\t1:1:1\n",good+"snes\t1:4:0\n",good+"snes\t2:0:0\n",good+"snes\t1::0\n",good+"snes\t1:0:0\textra\n",good+"no-tab\n",good+"# comment\n",good+" \n",header+"*\t1:1:1 \n",header+std::string("*\t1:1:1\0",8),std::string(4097,'\n')}){
        result=ui16_parse_crt_defaults(text,1);assert(result.state==Ui16CrtDefaultsState::Bad&&!result.recommendation.present);
    }
    std::string maximum=header;maximum.resize(4096,'\n');assert(ui16_parse_crt_defaults(maximum,1).state==Ui16CrtDefaultsState::Ready);
    assert(ui16_parse_crt_defaults(good,0).state==Ui16CrtDefaultsState::Bad);
    assert(ui16_parse_crt_defaults(good,256).state==Ui16CrtDefaultsState::Bad);
    setup(good);result=read();assert(result.source==Ui16CrtDefaultsSource::System&&opens==1&&reads==1&&closes==1&&path=="sd:/crt/defaults.tsv");
    setup(good);result=read(2,"usb:folder/a.sfc");assert(result.source==Ui16CrtDefaultsSource::Global&&path=="usb:/crt/defaults.tsv"&&opens==1);
    for(FRESULT missing:{FR_NO_FILE,FR_NO_PATH}){setup(good);open_result=missing;assert(read().state==Ui16CrtDefaultsState::Missing&&opens==1&&closes==0);}
    setup(good);open_result=FR_DISK_ERR;assert(read().state==Ui16CrtDefaultsState::Io&&closes==0);
    setup(good);read_error=true;assert(read().state==Ui16CrtDefaultsState::Io&&closes==1);
    setup(good);close_error=true;assert(read().state==Ui16CrtDefaultsState::Io&&closes==1);
    setup(good);early=true;assert(read().state==Ui16CrtDefaultsState::Bad&&closes==1);
    setup(good);oversize=true;assert(read().state==Ui16CrtDefaultsState::Bad&&closes==1);
    setup(good);advertised++;assert(read().state==Ui16CrtDefaultsState::Bad);
    setup(good);advertised--;assert(read().state==Ui16CrtDefaultsState::Bad);
    setup("");assert(read().state==Ui16CrtDefaultsState::Bad&&closes==1);
    setup(maximum);assert(read().state==Ui16CrtDefaultsState::Ready);
    setup(maximum+"\n");assert(read().state==Ui16CrtDefaultsState::Bad&&reads==0&&closes==1);
    setup(maximum+"\n");close_error=true;assert(read().state==Ui16CrtDefaultsState::Io&&reads==0&&closes==1);
    for(const char *rom:{"flash:a.nes","","x"}){setup(good);assert(read(1,rom).state==Ui16CrtDefaultsState::Unavailable&&opens==0);}
    setup(good);assert(read(0).state==Ui16CrtDefaultsState::Unavailable&&opens==0);
    assert(ui16_read_crt_defaults(1,nullptr,file,buffer.data(),8192).state==Ui16CrtDefaultsState::Unavailable&&opens==0);
    assert(ui16_read_crt_defaults(1,"sd:x",file,nullptr,8192).state==Ui16CrtDefaultsState::Unavailable&&opens==0);
    assert(ui16_read_crt_defaults(1,"sd:x",file,buffer.data(),4096).state==Ui16CrtDefaultsState::Unavailable&&opens==0);
    std::puts("CRT defaults PASS: atomic bounded TSV, BOM/CRLF, all system keys/global/blank/explicit OFF, duplicate/unknown/invalid rejection, volume separation, read-only aligned resources, exact length and open/read/close failure fallback");
}
