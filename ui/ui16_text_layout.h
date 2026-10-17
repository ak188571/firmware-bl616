#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

inline constexpr int UI16_FIRST_LINE_UNITS = 30;
inline constexpr int UI16_SECOND_LINE_UNITS = 29;

inline std::uint32_t ui16_next_utf8(std::string_view s, std::size_t &pos)
{
    const auto first=static_cast<unsigned char>(s[pos]);
    if(first<0x80){++pos;return first;}
    const int n=first>=0xc2&&first<=0xdf?2:first>=0xe0&&first<=0xef?3:first>=0xf0&&first<=0xf4?4:0;
    if(!n||pos+n>s.size()){++pos;return 0xfffd;}
    std::uint32_t cp=first&(0x7f>>n);
    for(int i=1;i<n;++i){const auto c=static_cast<unsigned char>(s[pos+i]);if((c&0xc0)!=0x80){++pos;return 0xfffd;}cp=(cp<<6)|(c&63);}
    if((n==2&&cp<0x80)||(n==3&&cp<0x800)||(n==4&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)){++pos;return 0xfffd;}
    pos+=n;return cp;
}

inline bool ui16_blank_line(std::string_view s)
{
    for(std::size_t p=0;p<s.size();){const auto cp=ui16_next_utf8(s,p);if(cp!=32&&cp!=0x3000)return false;}
    return true;
}
inline int ui16_text_units(std::string_view s)
{
    int units=0;
    for(std::size_t p=0;p<s.size();)units+=ui16_next_utf8(s,p)<128?1:2;
    return units;
}

// Input is decoded display text. Views always end at UTF-8 character boundaries.
struct Ui16TextLayout {
    std::string_view lines[2];
    int count=1;
    bool forced=false;
    bool valid=true;
    bool clipped=false;
};
inline Ui16TextLayout ui16_layout_text(std::string_view text)
{
    Ui16TextLayout out;
    const auto split=text.find('\n');
    if(split!=std::string_view::npos){
        out.forced=true;out.count=2;
        out.lines[0]=text.substr(0,split);out.lines[1]=text.substr(split+1);
        out.valid=text.find('\n',split+1)==std::string_view::npos &&
            !ui16_blank_line(out.lines[0])&&!ui16_blank_line(out.lines[1])&&
            ui16_text_units(out.lines[0])<=UI16_FIRST_LINE_UNITS&&
            ui16_text_units(out.lines[1])<=UI16_SECOND_LINE_UNITS;
        return out;
    }
    std::size_t pos=0;
    for(int row=0;row<2;++row){
        const auto start=pos;int units=0;
        while(pos<text.size()){
            auto end=pos;const int width=ui16_next_utf8(text,end)<128?1:2;
            if(units+width>(row?UI16_SECOND_LINE_UNITS:UI16_FIRST_LINE_UNITS))break;
            pos=end;units+=width;
        }
        out.lines[row]=text.substr(start,pos-start);
        if(pos==text.size()){out.count=row+1;return out;}
    }
    out.count=2;out.clipped=true;return out;
}

// CSV stores literal backslash-n; actual line separators remain record delimiters.
inline bool ui16_decode_title(std::string_view raw,std::string &decoded)
{
    decoded.clear();
    for(std::size_t i=0;i<raw.size();++i){
        if(raw[i]!='\\'){decoded+=raw[i];continue;}
        if(++i==raw.size())return false;
        if(raw[i]=='n')decoded+='\n';
        else if(raw[i]=='\\')decoded+='\\';
        else return false;
    }
    return ui16_layout_text(decoded).valid;
}

// top is an item index; only whole items fit into the ten available text rows.
template<class Height>
inline int ui16_variable_scroll_top(int selected,int count,int top,Height height)
{
    if(count<=0)return 0;
    int last=count-1,used=height(last);
    while(last>0&&used+height(last-1)<=10){--last;used+=height(last);}
    top=std::max(0,std::min(top,last));
    if(selected<0||selected>=count)return top;
    if(selected<top)return selected;
    used=0;for(int i=top;i<=selected;++i)used+=height(i);
    while(top<selected&&used>10)used-=height(top++);
    return top;
}
