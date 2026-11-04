#include "ui16_crt_save.h"
#include <cassert>
#include <array>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
using Bytes=std::vector<unsigned char>;
static std::map<std::string,Bytes> files;
static FIL file;
alignas(64) static std::array<unsigned char,8192> buffer;
static Ui16RomIdentity id{true,Ui16RomVolume::Sd,1,0x7623dffd,65552};
static const std::string prefix="sd:/crt/01-7623dffd-0000000000010010";
static std::string path,fault;
static bool active=false,writing=false;
static unsigned writes=0,syncs=0,mkdirs=0,opens=0;
static std::vector<std::string> targets;
extern "C" FRESULT f_open(FIL *f,const char *name,unsigned char mode){
    assert(f==&file&&!active);++opens;path=name;writing=(mode&FA_WRITE)!=0;
    if(writing){
        assert(mode==(FA_WRITE|FA_CREATE_ALWAYS));
        if(fault=="writeopen")return FR_DISK_ERR;
        files[path].clear();targets.push_back(path);
    }else{
        assert(mode==FA_READ);
        if(fault=="preread"||(writes&&fault=="verifyopen"))return FR_DISK_ERR;
        if(!files.count(path))return FR_NO_FILE;
    }
    active=true;f->position=0;f->size=files[path].size();return FR_OK;
}
extern "C" FRESULT f_read(FIL *f,void *out,UINT wanted,UINT *count){
    assert(f==&file&&active&&!writing&&out==buffer.data()&&wanted==33);*count=0;
    if(writes&&fault=="verifyread")return FR_DISK_ERR;
    *count=static_cast<UINT>(std::min<std::size_t>(wanted,files[path].size()));
    if(*count)std::memcpy(out,files[path].data(),*count);
    if(writes&&fault=="verifybytes"&&*count)static_cast<unsigned char *>(out)[0]^=1;
    return FR_OK;
}
extern "C" FRESULT f_close(FIL *f){
    assert(f==&file&&active);active=false;
    if((writing&&fault=="close")||(!writing&&writes&&fault=="verifyclose"))return FR_DISK_ERR;
    return FR_OK;
}
extern "C" FRESULT f_write(FIL *f,const void *data,UINT wanted,UINT *count){
    assert(f==&file&&active&&writing&&data==buffer.data()&&wanted==32);++writes;
    *count=fault=="short"?31:fault=="write"?3:32;
    const auto *bytes=static_cast<const unsigned char *>(data);files[path]=Bytes(bytes,bytes+*count);
    return fault=="write"?FR_DISK_ERR:FR_OK;
}
extern "C" FRESULT f_sync(FIL *f){assert(f==&file&&active&&writing);++syncs;return fault=="sync"?FR_DISK_ERR:FR_OK;}
extern "C" FRESULT f_mkdir(const char *name){assert(!active&&std::string(name)=="sd:/crt");++mkdirs;return fault=="mkdir"?FR_DISK_ERR:FR_EXIST;}
static void put(Bytes &r,unsigned at,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)r[at+i]=static_cast<unsigned char>(v>>(8*i));}
static void crc(Bytes &r){put(r,28,ui16_rom_crc(0xffffffffu,r.data(),28)^0xffffffffu,4);}
static Bytes record(std::uint32_t generation=7,unsigned value=23){
    Bytes r(32);r[0]='C';r[1]='R';r[2]='T';r[3]='1';r[4]=1;r[5]=1;
    put(r,8,id.crc,4);put(r,12,id.size,8);r[20]=static_cast<unsigned char>(value);put(r,24,generation,4);crc(r);return r;
}
static void setup(){assert(!active);files.clear();fault.clear();writes=syncs=mkdirs=opens=0;targets.clear();}
static bool save(const Ui16CrtSettings &value={true,3,3}){const bool ok=ui16_save_crt_override(id,value,file,buffer.data(),buffer.size());assert(!active);return ok;}
static bool reset_save(){const bool ok=ui16_save_crt_override(id,{true,2,3},file,buffer.data(),buffer.size(),true);assert(!active);return ok;}
static Ui16CrtRestore restore(){return ui16_read_crt_override(id,file,buffer.data(),buffer.size());}
int main(){
    setup();assert(save()&&writes==1&&syncs==1&&targets.back()==prefix+".a");
    auto result=restore();assert(result.override_value.settings==Ui16CrtSettings({true,3,3})&&result.generation==1&&result.selected_slot==0);
    const auto first=files[prefix+".a"];
    assert(save({false,0,0})&&files[prefix+".a"]==first&&targets.back()==prefix+".b");
    result=restore();assert(result.override_value.present&&result.override_value.settings==Ui16CrtSettings({false,0,0})&&result.generation==2);
    assert(files[prefix+".b"][6]==0); // Explicit OFF remains an override, never a Reset.
    assert(save({true,1,2})&&targets.back()==prefix+".a"&&restore().generation==3);
    for(const char *fail:{"mkdir","writeopen","write","short","sync","close","verifyopen","verifyread","verifyclose","verifybytes"}){
        setup();const auto old=record();files[prefix+".a"]=old;fault=fail;
        assert(!save()&&files[prefix+".a"]==old);
        if(writes)assert(targets.back()==prefix+".b");
        if(fault=="write"||fault=="short"){fault.clear();assert(restore().generation==7);} // Torn inactive slot.
    }
    // Failed sync/close may still leave a complete record. Re-read on retry,
    // preserve it, and write the other slot with a newer generation.
    for(const char *fail:{"sync","close","verifyread"}){
        setup();files[prefix+".a"]=record();fault=fail;assert(!save());
        const auto uncertain=files[prefix+".b"];fault.clear();assert(save({true,1,1}));
        assert(targets.back()==prefix+".a"&&files[prefix+".b"]==uncertain&&restore().generation==9);
    }
    setup();files[prefix+".a"]=record(0xffffffffu);assert(save()&&restore().generation==0);
    setup();files[prefix+".a"]=Bytes(32);files[prefix+".b"]=record();const auto good=files[prefix+".b"];
    assert(save()&&files[prefix+".b"]==good&&targets.back()==prefix+".a");
    setup();files[prefix+".a"]=Bytes(32);assert(save()&&restore().generation==1);
    for(unsigned blocked=0;blocked<4;++blocked){
        setup();files[prefix+".a"]=record();auto other=record();
        if(blocked==0)fault="preread";
        if(blocked==1){other[4]=2;crc(other);files[prefix+".b"]=other;}
        if(blocked==2){other[20]=0;crc(other);files[prefix+".b"]=other;}
        if(blocked==3)files[prefix+".b"]=record(0x80000007u);
        const auto before=files;assert(!save()&&!reset_save()&&files==before&&mkdirs==0&&writes==0);
    }
    setup();assert(!save({true,4,1})&&!save({true,1,4})&&opens==0);
    auto invalid=id;invalid.valid=false;
    assert(!ui16_save_crt_override(invalid,{},file,buffer.data(),buffer.size())&&opens==0);
    assert(!ui16_save_crt_override(id,{},file,nullptr,8192)&&opens==0);
    assert(!ui16_save_crt_override(id,{},file,buffer.data(),32)&&opens==0);
    setup();files[prefix+".a"]=record();const auto preserved_user=files[prefix+".a"];
    assert(reset_save()&&files[prefix+".a"]==preserved_user&&targets.back()==prefix+".b");
    result=restore();assert(result.state==Ui16CrtReadState::Reset&&!result.override_value.present&&result.generation==8);
    assert(files[prefix+".b"][6]==1&&files[prefix+".b"][20]==0); // Never encode a numeric recommendation in a Reset.
    const auto tombstone=files[prefix+".b"];assert(save({true,1,2})&&files[prefix+".b"]==tombstone);
    result=restore();assert(result.state==Ui16CrtReadState::User&&result.generation==9);
    setup();assert(reset_save()&&restore().state==Ui16CrtReadState::Reset&&restore().generation==1);
    setup();files[prefix+".a"]=record(0xffffffffu);assert(reset_save()&&restore().generation==0);
    for(const char *fail:{"mkdir","writeopen","write","short","sync","close","verifyopen","verifyread","verifyclose","verifybytes"}){
        setup();const auto old=record();files[prefix+".a"]=old;fault=fail;
        assert(!reset_save()&&files[prefix+".a"]==old);
        if(writes)assert(targets.back()==prefix+".b");
        if(fault=="write"||fault=="short"){fault.clear();assert(restore().state==Ui16CrtReadState::Recovered&&restore().override_value.present&&restore().generation==7);}
    }
    setup();files[prefix+".a"]=record();fault="sync";assert(!reset_save());
    const auto uncertain_reset=files[prefix+".b"];fault.clear();assert(reset_save());
    assert(files[prefix+".b"]==uncertain_reset&&restore().generation==9&&restore().state==Ui16CrtReadState::Reset);
    std::puts("CRT save PASS: alternate-slot sync/close/exact readback, Reset tombstones/zero value/retry/user resave, explicit OFF, generation wrap, torn writes, all write/verification failures, retry protects uncertain generation, corrupt recovery, I/O/schema/conflict refusal and invalid identity");
}
