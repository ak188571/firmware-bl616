#include "snes_loader_platform.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <vector>
int loadsnes(const char *);
char *strcasestr(const char *text,const char *part){
    for(const char *p=text;*p;p++){
        unsigned i=0;
        while(part[i]&&p[i]&&std::tolower(static_cast<unsigned char>(part[i]))==std::tolower(static_cast<unsigned char>(p[i])))i++;
        if(!part[i])return const_cast<char *>(p);
    }
    return nullptr;
}
bool core_running=false;
FIL fcore{};
alignas(64) unsigned char fbuf[8192];
static std::vector<unsigned char> file,sent;
static int opens,closes,payload_reads,loading,starts,stops;
static int read_error=-1,short_read=-1,oversize=-1;
static bool fail_open=false,fail_close=false,fail_seek=false,in_payload=false;
#ifdef TC_ROM_UART
static bool new_capable=false,new_success=true;
static unsigned new_calls=0;
bool tc_rom_uart_capable(){return new_capable;}
bool tc_rom_uart_load_snes(FIL &fp,std::uint32_t off,std::uint32_t count,const std::uint8_t *header){
    assert(&fp==&fcore&&header==fbuf&&count==file.size()-off&&!core_running);
    ++new_calls;return new_success;
}
#endif
static void setup(unsigned kind=0,unsigned prefix=0){
    const unsigned size=kind==2?0x410000:0x10000;
    file.resize(size+prefix);
    for(unsigned i=0;i<file.size();i++)file[i]=static_cast<unsigned char>(i*37+11);
    // Invalid candidate headers must not accidentally score as the other mapper.
    for(unsigned at:{0x7fc0u,0xffc0u,0x40ffc0u})if(at+prefix+64<=file.size())
        std::fill_n(file.begin()+at+prefix,64,0);
    const unsigned offset=(kind==0?0x7fc0:kind==1?0xffc0:0x40ffc0)+prefix;
    std::fill_n(file.begin()+offset,64,0);
    std::fill_n(file.begin()+offset,21,'G');
    file[offset+21]=static_cast<unsigned char>(kind);
    file[offset+23]=kind==2?13:6;
    file[offset+28]=0x12;file[offset+29]=0x34;file[offset+30]=0xed;file[offset+31]=0xcb;
    file[offset+61]=0x80;
    sent.clear();opens=closes=payload_reads=loading=starts=stops=0;
    core_running=true;fail_open=fail_close=fail_seek=in_payload=false;
    read_error=short_read=oversize=-1;
}
extern "C" FRESULT f_open(FIL *fp,const char *,unsigned char mode){
    assert(!core_running&&fp==&fcore&&mode==FA_READ);opens++;
    if(fail_open)return FR_DISK_ERR;
    fp->size=file.size();fp->position=0;return FR_OK;
}
extern "C" FRESULT f_lseek(FIL *fp,FSIZE_t at){
    assert(fp==&fcore);if(fail_seek)return FR_DISK_ERR;
    fp->position=std::min(at,file.size());in_payload=at==(file.size()&0x3ff);return FR_OK;
}
extern "C" FRESULT f_read(FIL *fp,void *data,UINT wanted,UINT *count){
    assert(fp==&fcore&&data==fbuf&&wanted<=1024);*count=0;
    if(in_payload){
        const int call=payload_reads++;
        if(call==read_error)return FR_DISK_ERR;
        if(call==oversize){*count=wanted+1;return FR_OK;}
        *count=static_cast<UINT>(std::min<std::size_t>(wanted,file.size()-fp->position));
        if(call==short_read&&*count)--*count;
    }else *count=static_cast<UINT>(std::min<std::size_t>(wanted,file.size()-fp->position));
    if(*count)std::memcpy(data,file.data()+fp->position,*count);
    fp->position+=*count;return FR_OK;
}
extern "C" FRESULT f_close(FIL *fp){assert(fp==&fcore&&!core_running);closes++;return fail_close?FR_DISK_ERR:FR_OK;}
void send_fbuf_data(std::uint16_t length){assert(loading==1&&!core_running);sent.insert(sent.end(),fbuf,fbuf+length);}
void set_loading_state(int value){
    assert(!core_running);
    if(value){assert(loading==0);starts++;}else {assert(loading==1&&closes==1);stops++;}
    loading=value;
}
void delay(unsigned){}
void overlay_status(const char *,...){}
void overlay_printf(const char *,...){}
void overlay_message(const char *,int){}
void overlay(int value){assert(value==0&&core_running&&closes==1&&!loading);}
int main(){
    for(unsigned kind=0;kind<3;kind++)for(unsigned prefix:{0u,512u}){
        setup(kind,prefix);assert(loadsnes("sd:snes/synthetic.sfc")==0&&core_running);
        assert(opens==1&&closes==1&&starts==1&&stops==1&&!loading);
        assert(sent.size()==file.size()-prefix+64);
        const unsigned header=(kind==0?0x7fc0:kind==1?0xffc0:0x40ffc0)+prefix;
        assert(std::equal(sent.begin(),sent.begin()+64,file.begin()+header));
        assert(std::equal(sent.begin()+64,sent.end(),file.begin()+prefix));
    }
    for(int fail=0;fail<8;fail++){
        setup();
        if(fail==0)fail_open=true;
        if(fail==1)fail_seek=true;
        if(fail==2)read_error=0;
        if(fail==3)read_error=31;
        if(fail==4)short_read=0;
        if(fail==5)short_read=63;
        if(fail==6)oversize=0;
        if(fail==7)fail_close=true;
        assert(loadsnes("sd:snes/synthetic.sfc")!=0&&!core_running&&!loading);
        assert(closes==(fail_open?0:1));
    }
    setup();file[0x7fc0+23]=255;assert(loadsnes("sd:snes/bad.sfc")!=0&&!core_running&&closes==1);
    setup();file.resize(65536+128);assert(loadsnes("sd:snes/bad.sfc")!=0&&!core_running&&closes==1);
    setup();file.clear();assert(loadsnes("sd:snes/empty.smc")!=0&&!core_running&&closes==1);
    setup();assert(loadsnes("sd:snes/wrong.bin")!=0&&!core_running&&opens==0);
    std::puts("SNES loader PASS: Lo/Hi/ExHi, raw/copier512, exact transfer, open/seek/read/short/oversize/close failure, malformed size/header, no false running state");
#ifdef TC_ROM_UART
    for(bool successful:{false,true}){
        setup();new_capable=true;new_success=successful;new_calls=0;
        assert((loadsnes("sd:snes/synthetic.sfc")==0)==successful);
        assert(core_running==successful&&new_calls==1&&starts==0&&stops==0&&sent.empty()&&closes==1);
    }
    std::puts("Negotiated SNES path: success and failure never call legacy loading/data/release");
#endif
}
