#ifdef UI16_SYSTEM_MENU
#include "ui16_crt_restore_reader.h"
#include <array>
#include <algorithm>
#include <cstdio>

namespace {
using Record=std::array<unsigned char,32>;
enum class Read { Missing, Valid, Bad, Io, Future };
std::uint64_t number(const Record &r,unsigned at,unsigned n){
    std::uint64_t value=0;
    for(unsigned i=0;i<n;++i)value|=std::uint64_t(r[at+i])<<(8*i);
    return value;
}
Read read_record(const char *path,const Ui16RomIdentity &id,FIL &file,void *buffer,Record &record){
    const auto opened=f_open(&file,path,FA_READ);
    if(opened==FR_NO_FILE||opened==FR_NO_PATH)return Read::Missing;
    if(opened!=FR_OK)return Read::Io;
    const auto size=f_size(&file);UINT count=0;
    const auto result=f_read(&file,buffer,33,&count);
    const auto closed=f_close(&file);
    if(result!=FR_OK||closed!=FR_OK)return Read::Io;
    if(size!=32||count!=32)return Read::Bad;
    std::copy_n(static_cast<unsigned char *>(buffer),32,record.begin());
    if(record[0]!='C'||record[1]!='R'||record[2]!='T'||record[3]!='1'||
       (ui16_rom_crc(0xffffffffu,record.data(),28)^0xffffffffu)!=number(record,28,4))return Read::Bad;
    if(record[4]!=1)return Read::Future;
    if(record[5]!=id.system||record[6]>1||record[7]||record[20]>31||record[21]||record[22]||record[23]||
       (record[6]==1&&record[20])||number(record,8,4)!=id.crc||number(record,12,8)!=id.size)return Read::Bad;
    return Read::Valid;
}
}

Ui16CrtRestore ui16_read_crt_override(const Ui16RomIdentity &id,FIL &file,void *buffer,UINT capacity){
    Ui16CrtRestore result;
    if(!id.valid||!id.size||id.system==0||id.system>255||!buffer||capacity<33||
       (id.volume!=Ui16RomVolume::Sd&&id.volume!=Ui16RomVolume::Usb))return result;
    Record records[2]{};Read states[2]{};char path[64];
    for(unsigned slot=0;slot<2;++slot){
        std::snprintf(path,sizeof(path),"%s/crt/%02x-%08lx-%016llx.%c",
            id.volume==Ui16RomVolume::Sd?"sd:":"usb:",id.system,
            static_cast<unsigned long>(id.crc),static_cast<unsigned long long>(id.size),slot?'b':'a');
        states[slot]=read_record(path,id,file,buffer,records[slot]);
    }
    // Partial visibility and unknown schema never justify choosing a stale value.
    if(states[0]==Read::Io||states[1]==Read::Io){result.state=Ui16CrtReadState::Io;return result;}
    if(states[0]==Read::Future||states[1]==Read::Future){result.state=Ui16CrtReadState::Future;return result;}
    int best=states[0]==Read::Valid?0:states[1]==Read::Valid?1:-1;
    if(states[0]==Read::Valid&&states[1]==Read::Valid){
        const auto difference=std::uint32_t(number(records[1],24,4)-number(records[0],24,4));
        if(difference==0x80000000u||(difference==0&&records[0]!=records[1])){
            result.state=Ui16CrtReadState::Conflict;return result;
        }
        if(difference&&difference<0x80000000u)best=1;
    }
    const bool bad=states[0]==Read::Bad||states[1]==Read::Bad;
    if(best<0){result.state=bad?Ui16CrtReadState::Bad:Ui16CrtReadState::Missing;return result;}
    const auto &r=records[best];const bool user=r[6]==0;
    result.selected_slot=best;
    result.generation=static_cast<std::uint32_t>(number(r,24,4));
    result.state=bad?Ui16CrtReadState::Recovered:user?Ui16CrtReadState::User:Ui16CrtReadState::Reset;
    if(user)result.override_value={true,{bool(r[20]&1),unsigned((r[20]>>1)&3),unsigned((r[20]>>3)&3)}};
    return result;
}

#endif
