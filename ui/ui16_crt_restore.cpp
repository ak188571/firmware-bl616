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
    const unsigned schema=record[4] & ~(UI16_CRT_BLOOM_FLAG | UI16_CRT_PERSISTENCE_FLAG | UI16_CRT_SPOT_FLAG | UI16_CRT_DISPLAY_FLAG);
    if(schema==1 && record[4]!=1)return Read::Future;
    const bool signal=schema==UI16_CRT_SIGNAL_RECORD || schema==UI16_CRT_CURVE_RECORD || schema==UI16_CRT_CONVERGENCE_RECORD || schema==UI16_CRT_NTSC_RECORD;
    if(!signal && schema!=1 && schema!=UI16_CRT_EXT_RECORD && schema!=UI16_CRT_GLOW_RECORD)return Read::Future;
    const unsigned system=signal?(record[5]&UI16_CRT_SIGNAL_SYSTEM_MASK):record[5];
    if(signal && (((record[5]&0x80) && schema!=UI16_CRT_CONVERGENCE_RECORD && schema!=UI16_CRT_NTSC_RECORD) || (system!=1 && system!=2)))return Read::Bad;
    const bool display=(record[4] & UI16_CRT_DISPLAY_FLAG)!=0;
    if(display && !number(record,16,4) && ((number(record,6,2)>>6)&3)!=3)return Read::Future;
    if(display && (number(record,16,4) & ~std::uint64_t(Ui16CrtSettings::display_mask(Ui16CrtSettings::DisplayRevision))))return Read::Future;
    if(system!=id.system||number(record,8,4)!=id.crc||number(record,12,display?4:8)!=id.size)return Read::Bad;
    if(schema==1){
        if(record[6]>1||record[7]||record[20]>31||record[21]||record[22]||record[23]||
           (record[6]==1&&record[20]))return Read::Bad;
    }else{
        const auto flags=Ui16CrtSettings::from_flags(number(record,6,2));
        if(!flags.valid() || (!display && flags.mask==3))return Read::Bad;
    }
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
    const auto &r=records[best];const unsigned schema=r[4] & ~(UI16_CRT_BLOOM_FLAG | UI16_CRT_PERSISTENCE_FLAG | UI16_CRT_SPOT_FLAG | UI16_CRT_DISPLAY_FLAG);
    const bool user=schema!=1 || r[6]==0;
    result.selected_slot=best;
    result.generation=static_cast<std::uint32_t>(number(r,24,4));
    result.state=bad?Ui16CrtReadState::Recovered:user?Ui16CrtReadState::User:Ui16CrtReadState::Reset;
    if(user){
        auto value=Ui16CrtSettings::from_flags(schema==1?r[20]:number(r,6,2));
        if(r[4]&UI16_CRT_DISPLAY_FLAG)value.display_options=static_cast<std::uint32_t>(number(r,16,4));
        value.spot=(r[4] & UI16_CRT_SPOT_FLAG)!=0;
        value.persistence=(r[4] & UI16_CRT_PERSISTENCE_FLAG)!=0;
        value.bloom=(r[4] & UI16_CRT_BLOOM_FLAG)!=0;
        value.glow=schema==UI16_CRT_GLOW_RECORD;
        value.ntsc=schema==UI16_CRT_NTSC_RECORD;
        value.convergence=schema==UI16_CRT_CONVERGENCE_RECORD || (value.ntsc && (r[5]&4));
        value.curvature=schema==UI16_CRT_CURVE_RECORD || ((value.ntsc || value.convergence) && (r[5]&0x80));
        if(schema==UI16_CRT_SIGNAL_RECORD || value.curvature || value.convergence || value.ntsc){
            value.composite=value.ntsc || (r[5]&4)!=0;
            value.composite_blur=(r[5]>>3)&3;
            value.composite_bleed=(r[5]&32)!=0;
            value.glow=(r[5]&64)!=0;
        }
        if(schema!=1)std::copy_n(r.begin()+20,4,value.widths.begin());
        result.override_value={true,value};
    }
    return result;
}

#endif
