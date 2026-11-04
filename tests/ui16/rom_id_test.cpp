#include "ui16_rom_id_reader.h"
#include <cassert>
#include <array>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <cstdio>

namespace {
std::vector<unsigned char> bytes;
unsigned opens=0,closes=0,reads=0,yields=0,active=0;
int read_error=-1,early_eof=-1,oversize=-1;
bool open_error=false,close_error=false;
std::uint64_t advertised=0;
std::string opened_path;
alignas(64) std::array<unsigned char,8192> buffer;
UINT capacity=8192;
void yielding(){assert(active==1);++yields;}
void setup(const std::vector<unsigned char> &data){
    assert(active==0);bytes=data;advertised=bytes.size();opens=closes=reads=yields=0;
    read_error=early_eof=oversize=-1;open_error=close_error=false;capacity=8192;
}
Ui16RomIdentity identify(int system=1,const char *path="sd:nes/game.nes"){
    FIL file{};const auto id=ui16_identify_rom(system,path,file,buffer.data(),capacity,yielding);
    assert(active==0);return id;
}
}
extern "C" FRESULT f_open(FIL *file,const char *path,unsigned char mode){
    ++opens;assert(active==0&&mode==FA_READ);opened_path=path;
    if(open_error)return FR_DISK_ERR;
    file->position=0;file->size=static_cast<std::size_t>(advertised);++active;return FR_OK;
}
extern "C" FRESULT f_read(FIL *file,void *data,UINT wanted,UINT *count){
    assert(active==1&&data==buffer.data()&&wanted==capacity);
    const int call=static_cast<int>(reads++);*count=0;
    if(call==read_error)return FR_DISK_ERR;
    if(call==early_eof)return FR_OK;
    if(call==oversize){*count=wanted+1;return FR_OK;}
    *count=static_cast<UINT>(std::min<std::size_t>(wanted,bytes.size()-file->position));
    if(*count)std::memcpy(data,bytes.data()+file->position,*count);
    file->position+=*count;return FR_OK;
}
extern "C" FRESULT f_close(FIL *){assert(active==1);--active;++closes;return close_error?FR_DISK_ERR:FR_OK;}

int main(){
    const std::string check="123456789";
    assert((ui16_rom_crc(0xffffffffu,check.data(),check.size())^0xffffffffu)==0xcbf43926u);
    setup(std::vector<unsigned char>(check.begin(),check.end()));
    auto original=identify();assert(original.valid&&original.crc==0xcbf43926u&&original.size==9&&original.system==1&&original.volume==Ui16RomVolume::Sd);
    assert(opens==1&&closes==1&&reads==2&&yields==1);
    setup(bytes);assert(original.same_game(identify(1,"sd:nes/renamed.nes")));
    setup(bytes);assert(!original.same_game(identify(2)));
    setup(bytes);assert(!original.same_game(identify(1,"usb:nes/game.nes")));
    setup(bytes);bytes[0]^=1;assert(!original.same_game(identify()));
    setup(std::vector<unsigned char>(check.begin(),check.end()));advertised++;assert(!identify().valid&&closes==1);
    setup(std::vector<unsigned char>(check.begin(),check.end()));advertised--;assert(!identify().valid&&closes==1);
    std::vector<unsigned char> rom(20003);for(std::size_t i=0;i<rom.size();++i)rom[i]=static_cast<unsigned char>(i*37+11);
    setup(rom);const auto large=identify();assert(large.valid&&large.size==rom.size()&&yields==3);
    for(unsigned chunk:{1u,127u,512u,8192u}){setup(rom);capacity=chunk;assert(large.same_game(identify()));}
    // Header/trainer and trailing bytes are part of the hash, not just game payload.
    for(unsigned changed:{0u,15u,16u,527u,20002u}){setup(rom);bytes[changed]^=1;assert(!large.same_game(identify()));}
    for(int fail=0;fail<4;++fail){setup(rom);read_error=fail;assert(!identify().valid&&closes==1);}
    for(int end=0;end<3;++end){setup(rom);early_eof=end;assert(!identify().valid&&closes==1);}
    setup(rom);oversize=0;assert(!identify().valid&&closes==1);
    setup(rom);close_error=true;assert(!identify().valid&&closes==1);
    setup(rom);open_error=true;assert(!identify().valid&&opens==1&&closes==0);
    setup({});assert(!identify().valid&&closes==1);
    for(const char *path:{"flash:/x.nes","bad",""}){setup(rom);assert(!identify(1,path).valid&&opens==0);}
    setup(rom);assert(!identify(0).valid&&opens==0);assert(!identify(256).valid&&opens==0);assert(!identify(1,nullptr).valid&&opens==0);
    FIL file{};assert(!ui16_identify_rom(1,"sd:x.nes",file,nullptr,8192).valid);assert(!ui16_identify_rom(1,"sd:x.nes",file,buffer.data(),0).valid);
    assert(!Ui16RomIdentity{}.same_game(Ui16RomIdentity{}));
    std::puts("ROM ID PASS: IEEE vector, full-file/header/trainer/tail hash, chunk independence, rename/system/volume separation, exact size, read-only buffers/yield, all I/O failures and stale identity rejection");
}
