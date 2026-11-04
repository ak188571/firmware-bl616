#ifdef UI16_SYSTEM_MENU
#include "ui16_crt_save.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>

bool ui16_save_crt_override(const Ui16RomIdentity &id,const Ui16CrtSettings &value,
                           FIL &file,void *buffer,UINT capacity){
    if(value.blur>3||value.scanline>3)return false;
    // Re-read before EVERY attempt. A previous failed sync/close/verification
    // may have produced a valid new generation; protect that generation too.
    const auto current=ui16_read_crt_override(id,file,buffer,capacity);
    if(current.state==Ui16CrtReadState::NoId||current.state==Ui16CrtReadState::Io||
       current.state==Ui16CrtReadState::Future||current.state==Ui16CrtReadState::Conflict)return false;
    const unsigned slot=current.selected_slot<0?0:1-current.selected_slot;
    const std::uint32_t generation=current.selected_slot<0?1:current.generation+1;
    std::array<unsigned char,32> record{};
    record[0]='C';record[1]='R';record[2]='T';record[3]='1';record[4]=1;record[5]=static_cast<unsigned char>(id.system);
    const auto put=[&](unsigned at,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)record[at+i]=static_cast<unsigned char>(v>>(8*i));};
    put(8,id.crc,4);put(12,id.size,8);
    record[20]=static_cast<unsigned char>(unsigned(value.enabled)|(value.blur<<1)|(value.scanline<<3));
    put(24,generation,4);put(28,ui16_rom_crc(0xffffffffu,record.data(),28)^0xffffffffu,4);
    char directory[12],path[64];
    std::snprintf(directory,sizeof(directory),"%s/crt",id.volume==Ui16RomVolume::Sd?"sd:":"usb:");
    const auto made=f_mkdir(directory);
    if(made!=FR_OK&&made!=FR_EXIST)return false;
    std::snprintf(path,sizeof(path),"%s/%02x-%08lx-%016llx.%c",directory,id.system,
        static_cast<unsigned long>(id.crc),static_cast<unsigned long long>(id.size),slot?'b':'a');
    if(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return false;
    std::memcpy(buffer,record.data(),record.size());UINT count=0;
    bool good=f_write(&file,buffer,32,&count)==FR_OK&&count==32;
    if(good&&f_sync(&file)!=FR_OK)good=false;
    if(f_close(&file)!=FR_OK)good=false;
    if(!good)return false;
    if(f_open(&file,path,FA_READ)!=FR_OK)return false;
    const auto size=f_size(&file);count=0;
    good=f_read(&file,buffer,33,&count)==FR_OK&&count==32&&size==32;
    if(f_close(&file)!=FR_OK)good=false;
    return good&&std::equal(record.begin(),record.end(),static_cast<unsigned char *>(buffer));
}
#endif
