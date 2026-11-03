#ifdef UI16_SYSTEM_MENU
#include "ui16_crt_store.h"
#include "ui16_titles.h"
#include <array>
#include <algorithm>
#include <cstdio>
extern "C" {
#include "ff.h"
}

std::uint32_t ui16_crt_crc(std::uint32_t state,const void *data,std::size_t size){
    const auto *bytes=static_cast<const unsigned char *>(data);
    for(std::size_t i=0;i<size;++i){state^=bytes[i];for(unsigned b=0;b<8;++b)state=(state>>1)^((state&1)?0xedb88320u:0);}
    return state;
}
namespace {
using Record=std::array<unsigned char,32>;
enum class Read { Missing,Valid,Bad,Io,Future };
const char *const systems[]={"*","nes","snes","gba","genesis","sms","pc"};
std::uint64_t number(const Record &r,unsigned at,unsigned n){
    std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(r[at+i])<<(8*i);return v;
}
void put(Record &r,unsigned at,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)r[at+i]=static_cast<unsigned char>(v>>(8*i));}
Ui16CrtSettings value(const Record &r){return {bool(r[20]&1),unsigned((r[20]>>1)&3),unsigned((r[20]>>3)&3)};}
bool newer(const Record &a,const Record &b){const auto d=std::uint32_t(number(a,24,4)-number(b,24,4));return d!=0&&d<0x80000000u;}
Read read_record(const std::string &path,int system,std::uint32_t crc,std::uint64_t size,Record &r){
    FIL f{};const auto opened=f_open(&f,path.c_str(),FA_READ);
    if(opened==FR_NO_FILE||opened==FR_NO_PATH)return Read::Missing;
    if(opened!=FR_OK)return Read::Io;
    std::array<unsigned char,33> bytes{};UINT n=0;
    const auto result=f_read(&f,bytes.data(),bytes.size(),&n);const auto closed=f_close(&f);
    if(result!=FR_OK||closed!=FR_OK)return Read::Io;
    if(n!=32)return Read::Bad;
    std::copy_n(bytes.begin(),32,r.begin());
    if(r[0]!='C'||r[1]!='R'||r[2]!='T'||r[3]!='1'||
       (ui16_crt_crc(0xffffffffu,r.data(),28)^0xffffffffu)!=number(r,28,4))return Read::Bad;
    if(r[4]!=1)return Read::Future;
    if(r[5]!=system||r[6]>1||r[7]||r[20]>31||r[21]||r[22]||r[23]||
       (r[6]==1&&r[20]!=0)||number(r,8,4)!=crc||number(r,12,8)!=size)return Read::Bad;
    return Read::Valid;
}
struct Pair {Record data[2]{};Read state[2]{};int best=-1;bool blocked=false;};
Pair pair(const std::string &prefix,int system,std::uint32_t crc,std::uint64_t size){
    Pair p;
    for(int i=0;i<2;++i){
        p.state[i]=read_record(prefix+(i?".b":".a"),system,crc,size,p.data[i]);
        if(p.state[i]==Read::Io||p.state[i]==Read::Future)p.blocked=true;
        if(p.state[i]==Read::Valid&&(p.best<0||newer(p.data[i],p.data[p.best])))p.best=i;
    }
    if(p.state[0]==Read::Valid&&p.state[1]==Read::Valid&&
       number(p.data[0],24,4)==number(p.data[1],24,4)&&p.data[0]!=p.data[1]){p.blocked=true;p.best=-1;}
    return p;
}
bool read_defaults(const std::string &path,int system,Ui16CrtSettings &out,const char *&source,bool &error){
    FIL f{};const auto opened=f_open(&f,path.c_str(),FA_READ);
    if(opened==FR_NO_FILE||opened==FR_NO_PATH)return false;
    if(opened!=FR_OK){error=true;return false;}
    std::string text;char buf[128];bool good=true;
    for(;;){UINT n=0;if(f_read(&f,buf,sizeof(buf),&n)!=FR_OK){good=false;break;}if(!n)break;
        text.append(buf,n);if(text.size()>4096){good=false;break;}}
    if(f_close(&f)!=FR_OK)good=false;
    if(text.compare(0,3,"\xef\xbb\xbf")==0)text.erase(0,3);
    std::array<Ui16CrtRecommendation,7> values{};bool seen[7]{};bool first=true;
    std::size_t at=0;
    while(good&&at<text.size()){
        const auto end=text.find('\n',at);std::string line=text.substr(at,end==std::string::npos?end:end-at);
        at=end==std::string::npos?text.size():end+1;if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(first){good=line=="system\tcrt_v1";first=false;continue;}
        if(line.empty())continue;
        const auto tab=line.find('\t');if(tab==std::string::npos){good=false;break;}
        int key=-1;for(int i=0;i<7;++i)if(line.substr(0,tab)==systems[i])key=i;
        if(key<0||seen[key]||!ui16_parse_crt(std::string_view(line).substr(tab+1),values[key])){good=false;break;}
        seen[key]=true;
    }
    if(!good||first){error=true;return false;}
    const int key=system>0&&system<7&&values[system].present?system:0;
    if(!values[key].present)return false;
    out=values[key].settings;source=key?"SYSTEM":"GLOBAL";return true;
}
}
void Ui16CrtStore::prepare(int system,const char *path,void (*yield)()){
    *this=Ui16CrtStore{};system_=system;
    const std::string rom=path?path:"";
    const auto colon=rom.find(':');
    if(system<=0||system>255||colon==std::string::npos||(rom.substr(0,colon)!="sd"&&rom.substr(0,colon)!="usb")){
        notice="NO ROM ID";return;
    }
    directory_=rom.substr(0,colon+1)+"/crt";
    bool bad_defaults=false;
    read_defaults(directory_+"/defaults.tsv",system,recommended,recommended_source,bad_defaults);
    if(bad_defaults)notice="BAD DEFAULTS";
    const auto slash=rom.find_last_of('/');
    if(slash!=std::string::npos){
        Ui16GameList one;one.status=Ui16ListStatus::Ready;one.names={rom.substr(slash+1)};
        ui16_load_titles((rom.substr(0,slash)+"/titles.csv").c_str(),one);
        if(one.titles_error)notice="BAD TITLES";
        else if(one.crt_recommendations.size()==1&&one.crt_recommendations[0].present){
            recommended=one.crt_recommendations[0].settings;recommended_source="GAME";
        }
    }
    initial=recommended;persisted_=initial;
    FIL f{};
    if(f_open(&f,rom.c_str(),FA_READ)!=FR_OK){notice="NO ROM ID";return;}
    const auto expected=std::uint64_t(f_size(&f));
    unsigned char buf[512];std::uint32_t crc=0xffffffffu;std::uint64_t count=0;bool good=true;
    for(;;){UINT n=0;if(f_read(&f,buf,sizeof(buf),&n)!=FR_OK){good=false;break;}if(!n)break;
        crc=ui16_crt_crc(crc,buf,n);count+=n;if(yield&&(count&8191u)==0)yield();}
    if(f_close(&f)!=FR_OK)good=false;
    if(!good||!count||count!=expected){notice="NO ROM ID";return;}
    crc_=crc^0xffffffffu;size_=count;ready=true;
    char key[48];std::snprintf(key,sizeof(key),"/%02x-%08lx-%016llx",static_cast<unsigned>(system),
        static_cast<unsigned long>(crc_),static_cast<unsigned long long>(size_));
    prefix_=directory_+key;
    const auto p=pair(prefix_,system_,crc_,size_);
    if(p.blocked)notice="STORE READ ERROR";
    else if(p.state[0]==Read::Bad||p.state[1]==Read::Bad)notice=p.best>=0?"RECOVERED":"BAD SAVE";
    if(p.best>=0){
        user_override=p.data[p.best][6]==0;
        if(user_override)initial=value(p.data[p.best]);
        persisted_=initial;
    }
}
bool Ui16CrtStore::dirty(const Ui16CrtSettings &v)const{
    const bool next_override=v!=recommended;
    return next_override!=user_override||(next_override&&v!=persisted_);
}
bool Ui16CrtStore::save(const Ui16CrtSettings &v){
    if(!ready||v.blur>3||v.scanline>3)return false;
    // Re-read before each attempt: a previous sync/close failure may have left a
    // valid new generation. Never overwrite the only known-good generation.
    const auto p=pair(prefix_,system_,crc_,size_);
    if(p.blocked)return false;
    const int target=p.best<0?0:1-p.best;
    const std::uint32_t generation=p.best<0?1:std::uint32_t(number(p.data[p.best],24,4)+1);
    const bool next_override=v!=recommended;
    Record r{};r[0]='C';r[1]='R';r[2]='T';r[3]='1';r[4]=1;r[5]=static_cast<unsigned char>(system_);r[6]=next_override?0:1;
    put(r,8,crc_,4);put(r,12,size_,8);r[20]=next_override?static_cast<unsigned char>(unsigned(v.enabled)|(v.blur<<1)|(v.scanline<<3)):0;
    put(r,24,generation,4);put(r,28,ui16_crt_crc(0xffffffffu,r.data(),28)^0xffffffffu,4);
    const auto made=f_mkdir(directory_.c_str());if(made!=FR_OK&&made!=FR_EXIST)return false;
    const std::string filename=prefix_+(target?".b":".a");FIL f{};
    if(f_open(&f,filename.c_str(),FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return false;
    UINT n=0;bool good=f_write(&f,r.data(),r.size(),&n)==FR_OK&&n==r.size();
    if(good&&f_sync(&f)!=FR_OK)good=false;
    if(f_close(&f)!=FR_OK)good=false;
    Record check{};
    if(!good||read_record(filename,system_,crc_,size_,check)!=Read::Valid||check!=r)return false;
    persisted_=v;user_override=next_override;notice="OK";return true;
}
#endif
