#ifdef UI16_SYSTEM_MENU
#include "ui16_crt_defaults_reader.h"
#include "ui16_text_layout.h"
#include <array>
#include <cstring>

namespace {
using View = std::string_view;
using Settings = Ui16CrtSettings;
constexpr View systems[]={"*","nes","snes","gba","genesis","sms","pc"};

template<std::size_t N>
int index_of(View text,const View (&names)[N]){
    for(std::size_t i=0;i<N;++i)if(names[i]==text)return static_cast<int>(i);
    return -1;
}

bool valid_key(View key){
    const auto letter=[](char c){return (c>='a' && c<='z') || (c>='A' && c<='Z');};
    if(key.empty() || !letter(key.front()))return false;
    for(char c:key)if(!letter(c) && !(c>='0' && c<='9') && c!='_')return false;
    return true;
}

// Unknown values still follow the checker's UTF-8, non-whitespace token syntax.
bool valid_value(View value){
    if(value.empty())return false;
    for(std::size_t pos=0;pos<value.size();){
        const auto start=pos;
        const auto c=ui16_next_utf8(value,pos);
        if((c==0xfffd && pos-start==1) || c=='=' || (c>=9 && c<=13) ||
           (c>=0x1c && c<=0x20) || c==0x85 || c==0xa0 || c==0x1680 ||
           (c>=0x2000 && c<=0x200a) || c==0x2028 || c==0x2029 || c==0x202f ||
           c==0x205f || c==0x3000)return false;
    }
    return true;
}

template<std::size_t N>
bool choice(unsigned &target,View value,const View (&choices)[N]){
    const int index=index_of(value,choices);
    if(index<0)return false;
    target=static_cast<unsigned>(index);
    return true;
}

void set_option(Settings &s,std::uint32_t bit,bool active){
    if(active)s.display_options|=bit;
    else s.display_options&=~bit;
}

bool apply_item(Settings &s,View key,View value,bool &known){
    known=true;
    static constexpr struct {View key; bool Settings::*field;} switches[]={
        {"crt",&Settings::enabled},{"beam",&Settings::beam},{"bleed",&Settings::bleed},
        {"composite_bleed",&Settings::composite_bleed},{"vignette",&Settings::vignette},
        {"corners",&Settings::rounded},{"overscan",&Settings::overscan},{"glow",&Settings::glow},
        {"curve",&Settings::curvature},{"beamspot",&Settings::spot},{"bloom",&Settings::bloom}
    };
    for(const auto &item:switches)if(item.key==key){
        if(value!="on" && value!="off")return false;
        s.*item.field=value=="on";
        return true;
    }
    static constexpr View strengths[]={"off","weak","std","strong"}, masks[]={"off","grille","shadow","slot"},
        gammas[]={"off","0.80","1.25"}, whites[]={"off","warm","cool"},
        signals[]={"rgb","composite","ntsc","rf"}, edges[]={"top","bottom","left","right"};
    if(key=="focus" || key=="phosphor"){
        if(value!="on" && value!="off")return false;
        set_option(s,key=="focus" ? Settings::EdgeFocus : Settings::PhosphorColor,value=="on");
        return true;
    }
    if(key=="pitch"){
        if(value!="1px" && value!="2px")return false;
        set_option(s,Settings::WidePitch,value=="2px");
        return true;
    }
    if(key=="afterglow" || key=="convergence"){
        const bool decay=key=="afterglow";
        const bool extended=value==(decay ? "rgb" : "pos");
        if(!extended && value!="on" && value!="off")return false;
        if(decay)s.persistence=value!="off";
        else s.convergence=value!="off";
        set_option(s,decay ? Settings::RgbDecay : Settings::PositionRgb,extended);
        return true;
    }
    if(key=="blur")return choice(s.blur,value,strengths);
    if(key=="composite_blur")return choice(s.composite_blur,value,strengths);
    if(key=="scan")return choice(s.scanline,value,strengths);
    if(key=="mask")return choice(s.mask,value,masks);
    if(key=="gamma")return choice(s.gamma,value,gammas);
    if(key=="white")return choice(s.temperature,value,whites);
    if(key=="signal"){
        const int signal=index_of(value,signals);
        if(signal<0)return false;
        s.composite=signal!=0;
        s.ntsc=signal>=2;
        set_option(s,Settings::RfSignal,signal==3);
        return true;
    }
    const int edge=index_of(key,edges);
    if(edge>=0){
        unsigned width=0;
        for(char c:value){
            if(c<'0' || c>'9')return false;
            width=width*10+static_cast<unsigned>(c-'0');
            if(width>255)return false;
        }
        s.widths[edge]=static_cast<std::uint8_t>(width);
        return true;
    }
    known=false;
    return true; // Well-formed future keys are ignored.
}

View take_token(View &row){
    const auto space=row.find(' ');
    const auto token=row.substr(0,space);
    row.remove_prefix(space==View::npos ? row.size() : space+1);
    return token;
}

bool apply_row(Settings &settings,View row,bool &has_known){
    const View original=row;
    while(!row.empty()){
        auto prior=original.substr(0,original.size()-row.size());
        const auto token=take_token(row);
        if(token.empty())continue;
        const auto eq=token.find('=');
        if(eq==View::npos)return false;
        const auto key=token.substr(0,eq),value=token.substr(eq+1);
        if(!valid_key(key) || !valid_value(value))return false;
        // Replay only this bounded row, including unknown keys, without allocating.
        while(!prior.empty()){
            const auto previous=take_token(prior);
            if(previous.substr(0,previous.find('='))==key)return false;
        }
        bool known;
        if(!apply_item(settings,key,value,known))return false;
        has_known|=known;
    }
    return true;
}

Ui16CrtDefaults parse_named(View text,unsigned system){
    Ui16CrtDefaults bad;
    bad.state=Ui16CrtDefaultsState::Bad;
    std::array<View,std::size(systems)> rows{};
    std::array<bool,std::size(systems)> seen{};
    while(!text.empty()){
        const auto end=text.find('\n');
        auto line=text.substr(0,end);
        text.remove_prefix(end==View::npos ? text.size() : end+1);
        if(!line.empty() && line.back()=='\r')line.remove_suffix(1);
        if(line.empty())continue;
        const auto tab=line.find('\t');
        if(tab==View::npos || line.find('\t',tab+1)!=View::npos)return bad;
        const int id=index_of(line.substr(0,tab),systems);
        if(id<0 || seen[id])return bad;
        seen[id]=true;
        rows[id]=line.substr(tab+1);
    }
    if(!seen[0])return bad;
    Ui16CrtDefaults result;
    for(std::size_t id=0;id<rows.size();++id){
        Settings unused;
        bool specified=false;
        const bool selected=id==0 || id==system;
        if(!apply_row(selected ? result.recommendation.settings : unused,rows[id],specified))return bad;
        if(selected && specified){
            result.recommendation.present=true;
            result.source=id==0 ? Ui16CrtDefaultsSource::Global : Ui16CrtDefaultsSource::System;
        }
    }
    result.state=Ui16CrtDefaultsState::Ready;
    result.named_items=true;
    return result;
}
}

Ui16CrtDefaults ui16_parse_crt_defaults(std::string_view text,unsigned system){
    Ui16CrtDefaults result;result.state=Ui16CrtDefaultsState::Bad;
    if(text.size()>4096||system==0||system>255)return result;
    if(text.substr(0,3)=="\xef\xbb\xbf")text.remove_prefix(3);
    const auto newline=text.find('\n');
    auto first=text.substr(0,newline);
    if(!first.empty() && first.back()=='\r')first.remove_suffix(1);
    if(first=="system\tcrt_v2")
        return parse_named(newline==View::npos ? View{} : text.substr(newline+1),system);
    std::array<Ui16CrtRecommendation,7> values{};bool seen[7]{};bool header=false;
    while(!text.empty()){
        const auto end=text.find('\n');auto line=text.substr(0,end);
        if(end==std::string_view::npos)text={};else text.remove_prefix(end+1);
        if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
        if(!header){if(line!="system\tcrt_v1")return result;header=true;continue;}
        if(line.empty())continue;
        const auto tab=line.find('\t');if(tab==std::string_view::npos)return result;
        const int key=index_of(line.substr(0,tab),systems);
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
