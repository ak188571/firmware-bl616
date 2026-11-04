#ifdef UI16_SYSTEM_MENU
#include "ui16_rom_id_reader.h"
#include <cstring>

std::uint32_t ui16_rom_crc(std::uint32_t state,const void *data,std::size_t size){
    const auto *bytes=static_cast<const unsigned char *>(data);
    for(std::size_t i=0;i<size;++i){
        state^=bytes[i];
        for(unsigned bit=0;bit<8;++bit)state=(state>>1)^((state&1)?0xedb88320u:0);
    }
    return state;
}

Ui16RomIdentity ui16_identify_rom(int system,const char *path,FIL &file,
                                void *buffer,UINT capacity,void (*yield)()){
    Ui16RomIdentity result;
    if(system<=0||system>255||!path||!buffer||!capacity)return result;
    Ui16RomVolume volume;
    if(std::strncmp(path,"sd:",3)==0)volume=Ui16RomVolume::Sd;
    else if(std::strncmp(path,"usb:",4)==0)volume=Ui16RomVolume::Usb;
    else return result;
    if(f_open(&file,path,FA_READ)!=FR_OK)return result;
    const auto expected=std::uint64_t(f_size(&file));
    std::uint64_t count=0;std::uint32_t crc=0xffffffffu;bool good=true;
    for(;;){
        UINT n=0;
        if(f_read(&file,buffer,capacity,&n)!=FR_OK){good=false;break;}
        if(!n)break;
        if(n>capacity||count>expected||std::uint64_t(n)>expected-count){good=false;break;}
        crc=ui16_rom_crc(crc,buffer,n);count+=n;
        if(yield)yield();
    }
    if(f_close(&file)!=FR_OK)good=false;
    if(good&&count&&count==expected){
        result.valid=true;result.volume=volume;result.system=static_cast<unsigned>(system);
        result.crc=crc^0xffffffffu;result.size=count;
    }
    return result;
}
#endif
