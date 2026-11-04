#ifdef UI16_SYSTEM_MENU
#include "ui16_crt_defaults_reader.h"
#include <array>
#include <cstring>

Ui16CrtDefaults ui16_parse_crt_defaults(std::string_view text,unsigned system){
    Ui16CrtDefaults result;result.state=Ui16CrtDefaultsState::Bad;
    if(text.size()>4096||system==0||system>255)return result;
    if(text.substr(0,3)=="\xef\xbb\xbf")text.remove_prefix(3);
    static const char *const keys[]={"*","nes","snes","gba","genesis","sms","pc"};
    std::array<Ui16CrtRecommendation,7> values{};bool seen[7]{};bool header=false;
    while(!text.empty()){
        const auto end=text.find('\n');auto line=text.substr(0,end);
        if(end==std::string_view::npos)text={};else text.remove_prefix(end+1);
        if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
        if(!header){if(line!="system\tcrt_v1")return result;header=true;continue;}
        if(line.empty())continue;
        const auto tab=line.find('\t');if(tab==std::string_view::npos)return result;
        int key=-1;for(unsigned i=0;i<7;++i)if(line.substr(0,tab)==keys[i])key=static_cast<int>(i);
        if(key<0||seen[key]||!ui16_parse_crt(line.substr(tab+1),values[key]))return result;
        seen[key]=true;
    }
    if(!header)return result;
    result.state=Ui16CrtDefaultsState::Ready;
    const unsigned selected=system<7&&values[system].present?system:0;
    if(values[selected].present){
        result.recommendation=values[selected];
        result.source=selected?Ui16CrtDefaultsSource::System:Ui16CrtDefaultsSource::Global;
    }
    return result;
}

Ui16CrtDefaults ui16_read_crt_defaults(int system,const char *rom_path,FIL &file,void *buffer,UINT capacity){
    Ui16CrtDefaults result;result.state=Ui16CrtDefaultsState::Unavailable;
    if(system<=0||system>255||!rom_path||!buffer||capacity<4097)return result;
    const char *path=nullptr;
    if(std::strncmp(rom_path,"sd:",3)==0)path="sd:/crt/defaults.tsv";
    else if(std::strncmp(rom_path,"usb:",4)==0)path="usb:/crt/defaults.tsv";
    else return result;
    const auto opened=f_open(&file,path,FA_READ);
    if(opened==FR_NO_FILE||opened==FR_NO_PATH){result.state=Ui16CrtDefaultsState::Missing;return result;}
    result.state=Ui16CrtDefaultsState::Io;
    if(opened!=FR_OK)return result;
    const auto expected=f_size(&file);UINT count=0;
    // Oversized files are rejected without reading them into the buffer.
    if(expected>4096){
        if(f_close(&file)==FR_OK)result.state=Ui16CrtDefaultsState::Bad;
        return result;
    }
    const auto read=f_read(&file,buffer,4097,&count);const auto closed=f_close(&file);
    if(read!=FR_OK||closed!=FR_OK)return result;
    if(count!=expected||count>4096){result.state=Ui16CrtDefaultsState::Bad;return result;}
    return ui16_parse_crt_defaults(std::string_view(static_cast<char *>(buffer),count),static_cast<unsigned>(system));
}
#endif
