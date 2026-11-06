#include "ui16_crt_restore_reader.h"
#include <cassert>
#include <array>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cstdio>
using Bytes=std::vector<unsigned char>;
static std::map<std::string,Bytes> files;
static std::vector<std::string> paths;
static std::string current,open_error,read_error,close_error;
static unsigned active=0,closes=0;
static bool early=false,oversize=false;
static FIL file;
alignas(64) static std::array<unsigned char,8192> buffer;
#ifndef TEST_CRT_SYSTEM
#define TEST_CRT_SYSTEM 1
#endif
static Ui16RomIdentity id{true,Ui16RomVolume::Sd,TEST_CRT_SYSTEM,0x7623dffd,65552};
static const std::string prefix=std::string("sd:/crt/0")+char('0'+TEST_CRT_SYSTEM)+"-7623dffd-0000000000010010";
extern "C" FRESULT f_open(FIL *f,const char *path,unsigned char mode){
    assert(!active&&f==&file&&mode==FA_READ);paths.push_back(path);current=path;
    if(current==open_error)return FR_DISK_ERR;
    if(!files.count(current))return FR_NO_FILE;
    ++active;f->position=0;f->size=files[current].size();return FR_OK;
}
extern "C" FRESULT f_read(FIL *f,void *out,UINT wanted,UINT *count){
    assert(active==1&&f==&file&&out==buffer.data()&&wanted==33);*count=0;
    if(current==read_error)return FR_DISK_ERR;
    if(early)return FR_OK;
    if(oversize){*count=34;return FR_OK;}
    const auto &data=files[current];*count=static_cast<UINT>(std::min<std::size_t>(wanted,data.size()));
    if(*count)std::memcpy(out,data.data(),*count);
    return FR_OK;
}
extern "C" FRESULT f_close(FIL *f){assert(active==1&&f==&file);--active;++closes;return current==close_error?FR_DISK_ERR:FR_OK;}
static void put(Bytes &r,unsigned at,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)r[at+i]=static_cast<unsigned char>(v>>(8*i));}
static void crc(Bytes &r){put(r,28,ui16_rom_crc(0xffffffffu,r.data(),28)^0xffffffffu,4);}
static Bytes record(std::uint32_t generation=1,unsigned kind=0,unsigned value=31){
    Bytes r(32);r[0]='C';r[1]='R';r[2]='T';r[3]='1';r[4]=1;r[5]=TEST_CRT_SYSTEM;r[6]=static_cast<unsigned char>(kind);
    put(r,8,id.crc,4);put(r,12,id.size,8);r[20]=static_cast<unsigned char>(value);put(r,24,generation,4);crc(r);return r;
}
static Ui16CrtRestore read(const Ui16RomIdentity &identity=id){
    const auto before=files;const auto result=ui16_read_crt_override(identity,file,buffer.data(),buffer.size());
    assert(!active&&files==before);return result;
}
static void setup(){assert(!active);files.clear();paths.clear();open_error=read_error=close_error="";closes=0;early=oversize=false;}
int main(){
    setup();assert(read().state==Ui16CrtReadState::Missing&&paths.size()==2);
    files[prefix+".a"]=record();auto restored=read();
    assert(restored.state==Ui16CrtReadState::User&&restored.override_value.present&&restored.override_value.settings==Ui16CrtSettings({true,3,3}));
    files[prefix+".b"]=record(2,0,0);restored=read();assert(restored.override_value.present&&restored.override_value.settings==Ui16CrtSettings({false,0,0}));
    files[prefix+".b"]=record(2,1,0);assert(read().state==Ui16CrtReadState::Reset&&!read().override_value.present);
    files[prefix+".a"]=record(0xffffffffu);files[prefix+".b"]=record(0,0,3);assert(read().override_value.settings.blur==1);
    files[prefix+".a"]=record(3);files[prefix+".b"]=record(3,0,0);assert(read().state==Ui16CrtReadState::Conflict&&!read().override_value.present);
    files[prefix+".b"]=record(0x80000003u);assert(read().state==Ui16CrtReadState::Conflict);
    files[prefix+".b"]=files[prefix+".a"];assert(read().state==Ui16CrtReadState::User);
    // Every record byte is protected; even matching filenames cannot bypass CRC.
    for(unsigned i=0;i<32;++i){setup();auto r=record();r[i]^=1;files[prefix+".a"]=r;assert(read().state==Ui16CrtReadState::Bad&&!read().override_value.present);}
    // Valid CRC does not excuse wrong schema, identity, kind, reserved bits, or tombstone value.
    for(unsigned at:{4u,5u,6u,7u,8u,12u,20u,21u,22u,23u}){
        setup();auto r=record();r[at]=at==4?2:at==6?2:at==20?32:static_cast<unsigned char>(r[at]^1);crc(r);files[prefix+".a"]=r;
        assert(read().state==(at==4?Ui16CrtReadState::Future:Ui16CrtReadState::Bad));
    }
    setup();files[prefix+".a"]=record(2,1,1);assert(read().state==Ui16CrtReadState::Bad);
    for(unsigned n:{0u,31u,33u,64u}){setup();auto r=record();r.resize(n);files[prefix+".a"]=r;assert(read().state==Ui16CrtReadState::Bad);}
    setup();files[prefix+".a"]=record();files[prefix+".b"]=Bytes(32);assert(read().state==Ui16CrtReadState::Recovered&&read().override_value.present);
    files[prefix+".a"]=record(1,1,0);assert(read().state==Ui16CrtReadState::Recovered&&!read().override_value.present);
    setup();files[prefix+".a"]=record();auto future=record();future[4]=2;crc(future);files[prefix+".b"]=future;
    assert(read().state==Ui16CrtReadState::Future&&!read().override_value.present);
    for(unsigned fail=0;fail<3;++fail){setup();files[prefix+".a"]=record();files[prefix+".b"]=record(2);
        (fail==0?open_error:fail==1?read_error:close_error)=prefix+".b";
        assert(read().state==Ui16CrtReadState::Io&&!read().override_value.present);
    }
    setup();files[prefix+".a"]=record();early=true;assert(read().state==Ui16CrtReadState::Bad);early=false;oversize=true;assert(read().state==Ui16CrtReadState::Bad);
    setup();auto other=id;other.valid=false;assert(read(other).state==Ui16CrtReadState::NoId&&paths.empty());
    other=id;other.volume=Ui16RomVolume::None;assert(read(other).state==Ui16CrtReadState::NoId&&paths.empty());
    other=id;other.system=256;assert(read(other).state==Ui16CrtReadState::NoId&&paths.empty());
    other=id;other.size=0;assert(read(other).state==Ui16CrtReadState::NoId&&paths.empty());
    assert(ui16_read_crt_override(id,file,buffer.data(),32).state==Ui16CrtReadState::NoId&&paths.empty());
    other=id;other.volume=Ui16RomVolume::Usb;assert(read(other).state==Ui16CrtReadState::Missing&&paths[0].find("usb:/crt/")==0);
    setup();other=id;other.system=TEST_CRT_SYSTEM==1?2:1;assert(read(other).state==Ui16CrtReadState::Missing&&paths[0].find(TEST_CRT_SYSTEM==1?"/02-":"/01-")!=std::string::npos);
    std::puts("CRT restore PASS: read-only aligned buffers, two generations/wrap/tombstones, explicit OFF, whole record integrity, schema/key/reserved validation, corrupt sibling recovery, I/O/future/conflict fallback, volume/system separation");
}
