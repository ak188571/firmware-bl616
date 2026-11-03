#pragma once
#include <cstdint>
#include <string_view>

struct Ui16CrtSettings {
    bool enabled=false;
    unsigned blur=0,scanline=0;
    std::uint32_t merge(std::uint32_t config)const {
        return (config&~0x001f0000u)|(std::uint32_t(enabled)<<16)|((blur&3u)<<17)|((scanline&3u)<<19);
    }
    bool operator==(const Ui16CrtSettings &v)const{return enabled==v.enabled&&blur==v.blur&&scanline==v.scanline;}
    bool operator!=(const Ui16CrtSettings &v)const{return !(*this==v);}
};
struct Ui16CrtRecommendation {bool present=false;Ui16CrtSettings settings;};
// Versioned titles/defaults field: enable:blur:scanline, or empty to inherit.
inline bool ui16_parse_crt(std::string_view text,Ui16CrtRecommendation &out){
    out={};if(text.empty())return true;
    if(text.size()!=5||text[0]<'0'||text[0]>'1'||text[1]!=':'||text[2]<'0'||text[2]>'3'||text[3]!=':'||text[4]<'0'||text[4]>'3')return false;
    out.present=true;out.settings={text[0]=='1',unsigned(text[2]-'0'),unsigned(text[4]-'0')};return true;
}
