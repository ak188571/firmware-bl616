#include "ui16_system_menu.h"
#include "ui16_font_cache.h"
#include "ui16_link.h"
#include "utils.h"
extern "C" {
#include "ff.h"
}
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

static std::vector<std::uint8_t> image;
static std::vector<unsigned> link_commands,raw_commands;
static bool link_available=true,fail_show=false,fail_row=false,fail_read=false;
static unsigned sd_reads=0;
int critical_depth=0;
extern "C" FRESULT f_open(FIL *f,const char *path,unsigned char mode){
    assert(mode==FA_READ);
    if(std::strcmp(path,"sd:/ui16/font16.ui16"))return FR_NO_FILE;
    f->position=0;f->size=image.size();return FR_OK;
}
extern "C" FRESULT f_read(FIL *f,void *dest,UINT n,UINT *actual){
    ++sd_reads;
    if(fail_read){fail_read=false;*actual=0;return FR_DISK_ERR;}
    *actual=static_cast<UINT>(std::min<std::size_t>(n,image.size()-f->position));
    std::memcpy(dest,image.data()+f->position,*actual);f->position+=*actual;return FR_OK;
}
extern "C" FRESULT f_lseek(FIL *f,FSIZE_t pos){
    if(pos>image.size())return FR_DISK_ERR;
    f->position=pos;return FR_OK;
}
extern "C" FRESULT f_close(FIL*){return FR_OK;}
void enter_critical(){assert(critical_depth++==0);}
void leave_critical(){assert(--critical_depth==0);}
void fpga_tx_header(int command,int){assert(critical_depth==1);raw_commands.push_back(unsigned(command));}
void fpga_tx_byte(std::uint8_t){assert(critical_depth==1);}
void ui16_link_monitor_reset(){}
bool ui16_link_prepare(){return link_available;}
bool ui16_link_send(std::uint8_t command,const std::uint8_t *payload,std::size_t){
    assert(link_available);
    link_commands.push_back(command);
    if(command==0x13 && payload[0]==1 && fail_show){fail_show=false;return false;}
    if(command==0x14 && fail_row){fail_row=false;return false;}
    return true;
}
static unsigned count(const std::vector<unsigned>& commands,unsigned c){
    return static_cast<unsigned>(std::count(commands.begin(),commands.end(),c));
}
static void clear(){link_commands.clear();raw_commands.clear();sd_reads=0;}
int main(int argc,char **argv){
    assert(argc==2);
    std::ifstream in(argv[1],std::ios::binary);assert(in);
    image={std::istreambuf_iterator<char>(in),{}};
    const Ui16MenuEntry entries[]={{3,u8"新天地"}};
    ui16_font_open("sd:/ui16/font16.ui16");
    clear();ui16_send_system_menu(entries,1,0);
    assert(count(link_commands,0x13)==2 && count(link_commands,0x12)==3 &&
           count(link_commands,0x14)==28 && raw_commands.empty() && sd_reads==3);
    clear();ui16_send_system_menu(entries,1,0);
    assert(count(link_commands,0x12)==0 && count(link_commands,0x13)==0 &&
           count(link_commands,0x14)==28 && sd_reads==0);
    link_available=false;
    clear();ui16_send_system_menu(entries,1,0);
    assert(link_commands.empty() && count(raw_commands,0x0f)==28);
    link_available=true;
    clear();ui16_send_system_menu(entries,1,0);
    assert(count(link_commands,0x13)==2 && count(link_commands,0x12)==3 &&
           count(link_commands,0x14)==28);
    ui16_font_open("sd:/ui16/font16.ui16");
    fail_row=true;
    clear();ui16_send_system_menu(entries,1,0);
    assert(count(link_commands,0x14)==1 && count(raw_commands,0x13)==2 &&
           count(raw_commands,0x14)==28 && count(raw_commands,0x0f)==0);
    ui16_font_open("sd:/ui16/font16.ui16");
    fail_show=true;
    clear();ui16_send_system_menu(entries,1,0);
    assert(count(link_commands,0x14)==28 && count(raw_commands,0x13)==2 &&
           count(raw_commands,0x14)==28 && count(raw_commands,0x0f)==0);
    ui16_font_open("sd:/ui16/font16.ui16");
    fail_read=true;
    clear();ui16_send_system_menu(entries,1,0);
    assert(link_commands.empty() && count(raw_commands,0x0f)==28);
    assert(critical_depth==0);
    std::puts("UI16 reliable menu: cached reads, version fallback, row/SHOW recovery and SD read fault passed");
}
