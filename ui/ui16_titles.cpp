#ifdef UI16_SYSTEM_MENU
#include "ui16_titles.h"
#include "ui16_text_layout.h"
#include <string_view>
#include <utility>
extern "C" {
#include "ff.h"
}
namespace {
bool equal_name(std::string_view a, std::string_view b) {
    if(a.size()!=b.size()) return false;
    for(std::size_t i=0;i<a.size();++i) {
        auto lower=[](unsigned char c){return c>='A'&&c<='Z'?c+32:c;};
        if(lower(a[i])!=lower(b[i])) return false;
    }
    return true;
}
bool valid_title(std::string_view s) {
    for(std::size_t i=0;i<s.size();) {
        const unsigned char a=s[i++];
        if(a<128) { if(a<32||a==127) return false; continue; }
        const int n=a>=0xc2&&a<=0xdf?2:a>=0xe0&&a<=0xef?3:a>=0xf0&&a<=0xf4?4:0;
        if(!n||i+n-1>s.size())return false;
        unsigned cp=a&(0x7f>>n);
        for(int j=1;j<n;++j) {const unsigned char c=s[i++];if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63);}
        if((n==2&&cp<128)||(n==3&&cp<0x800)||(n==4&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||(cp>=0x80&&cp<=0x9f))return false;
    }
    return true;
}
bool blank_title(std::string_view s) {
    while(!s.empty()) {
        if(s.front()==' ')s.remove_prefix(1);
        else if(s.substr(0,3)=="\xe3\x80\x80")s.remove_prefix(3);
        else return false;
    }
    return true;
}
}
Ui16TitlesParser::Ui16TitlesParser(Ui16GameList &games)
    : games_(games),pending_(games.names.size()),seen_(games.names.size(),false) {}
bool Ui16TitlesParser::line() {
    if(!line_.empty()&&line_.back()=='\r')line_.pop_back();
    if(first_) {
        first_=false;
        if(line_.compare(0,3,"\xef\xbb\xbf")==0)line_.erase(0,3);
        return line_=="name\ttitle";
    }
    if(line_.empty())return true;
    const auto tab=line_.find('\t');
    if(tab==std::string::npos||line_.find('\t',tab+1)!=std::string::npos)return false;
    const std::string_view name(line_.data(),tab),title(line_.data()+tab+1,line_.size()-tab-1);
    if(name.empty()||name.size()>255||name.back()==' '||name.back()=='.'||title.size()>UI16_TITLE_MAX_BYTES||!valid_title(title))return false;
    std::string decoded;
    if(!ui16_decode_title(title,decoded))return false;
    for(unsigned char c:name)if(c<32||c>126||std::string_view("<>:\"/\\|?*").find(c)!=std::string_view::npos)return false;
    for(std::size_t i=0;i<games_.names.size();++i)if(equal_name(name,games_.names[i])) {
        if(seen_[i])return false;
        seen_[i]=true;
        if(!blank_title(title)){
            const bool folder=i<games_.directories.size()&&games_.directories[i];
            if(!ui16_layout_text(decoded+(folder?"/":"")).valid)return false;
            pending_[i]=decoded;
        }
    }
    return true;
}
bool Ui16TitlesParser::feed(const char *data,std::size_t length) {
    if(failed_)return false;
    for(std::size_t i=0;i<length;++i) {
        if(++total_>UI16_TSV_MAX_BYTES){failed_=true;break;}
        if(data[i]=='\n') {
            if(!line()){failed_=true;break;}
            line_.clear();
        } else {
            line_+=data[i];
            if(line_.size()>769){failed_=true;break;}
        }
    }
    return !failed_;
}
bool Ui16TitlesParser::finish() {
    if(failed_||(!line_.empty()&&!line())||first_)return false;
    games_.titles=std::move(pending_);
    return true;
}
void ui16_load_titles(const char *path,Ui16GameList &games) {
    games.titles.clear();games.titles_error=false;
    if(games.status!=Ui16ListStatus::Ready)return;
    FIL file;
    const auto opened=f_open(&file,path,FA_READ);
    if(opened==FR_NO_FILE||opened==FR_NO_PATH)return;
    if(opened!=FR_OK){games.titles_error=true;return;}
    Ui16TitlesParser parser(games);
    char buffer[128];bool good=true;
    for(;;) {
        UINT n=0;
        if(f_read(&file,buffer,sizeof(buffer),&n)!=FR_OK){good=false;break;}
        if(!n)break;
        if(!parser.feed(buffer,n)){good=false;break;}
    }
    if(f_close(&file)!=FR_OK)good=false;
    if(!good||!parser.finish()){games.titles.clear();games.titles_error=true;}
}
#endif
