#include "ui16_system_menu.h"
#include "ui16_font_cache.h"
#include "ui16_font_map.h"
#include "utils.h"
extern "C" {
#include "ff.h"
}
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <iterator>

static std::vector<std::uint8_t> image;
static std::vector<std::uint8_t> wire;
static int depth=0;
static bool fail_on_blank=false;
static bool fail_next_read=false;
static unsigned sd_reads=0,sd_seeks=0;
extern "C" FRESULT f_open(FIL *fp, const char *path, unsigned char mode){
    assert(mode==FA_READ);
    if(std::strcmp(path,"sd:/ui16/font16.ui16"))return FR_NO_FILE;
    fp->position=0;fp->size=image.size();return FR_OK;
}
extern "C" FRESULT f_read(FIL *fp, void *dest, UINT count, UINT *actual){
    ++sd_reads;
    if(fail_next_read){fail_next_read=false;*actual=0;return FR_DISK_ERR;}
    *actual=static_cast<UINT>(fp->position<image.size()?std::min<std::size_t>(count,image.size()-fp->position):0);
    if(*actual)std::memcpy(dest,image.data()+fp->position,*actual);
    fp->position+=*actual;return FR_OK;
}
extern "C" FRESULT f_lseek(FIL *fp,FSIZE_t pos){
    ++sd_seeks;
    if(pos>image.size())return FR_DISK_ERR;
    fp->position=pos;return FR_OK;
}
extern "C" FRESULT f_close(FIL*){return FR_OK;}
void enter_critical(){assert(depth++==0);}
void leave_critical(){assert(--depth==0);}
void fpga_tx_byte(std::uint8_t b){assert(depth==1);wire.push_back(b);}
void fpga_tx_header(int command,int len){
    if(fail_on_blank && command==0x13){
        fail_next_read=true;fail_on_blank=false;
    }
    fpga_tx_byte(0xaa);fpga_tx_byte(len>>8);fpga_tx_byte(len);fpga_tx_byte(command);
}
static std::uint8_t crc8(const std::uint8_t *bytes,std::size_t count){
    unsigned crc=0;
    for(std::size_t i=0;i<count;++i){
        crc^=bytes[i];
        for(int bit=0;bit<8;++bit)crc=(crc&0x80)?((crc<<1)^0x07)&255:(crc<<1)&255;
    }
    return static_cast<std::uint8_t>(crc);
}
static void validate_verified_rows(){
    int total=0;
    for(std::size_t p=0;p<wire.size();){
        assert(wire[p]==0xaa && p+4<=wire.size());
        const auto len=(unsigned(wire[p+1])<<8)|wire[p+2];
        assert(len && p+3+len<=wire.size());
        if(wire[p+3]==0x14){
            assert(len==69);
            assert(wire[p+6]==32); // row header: y, x, cell count
            assert(wire[p+3+len-1]==crc8(wire.data()+p+4,len-2));
            ++total;
        }
        p+=3+len;
    }
    assert(total==28);
}
static int count_command(unsigned command){
    int n=0;
    for(std::size_t p=0;p<wire.size();){
        assert(wire[p]==0xaa && p+4<=wire.size());
        const auto length=(unsigned(wire[p+1])<<8)|wire[p+2];
        assert(length && p+3+length<=wire.size());
        if(wire[p+3]==command)++n;
        p+=3+length;
    }
    return n;
}
int main(int argc,char**argv){
    assert(argc==2 || argc==3);std::ifstream in(argv[1],std::ios::binary);assert(in);
    image={std::istreambuf_iterator<char>(in),{}};
    ui16_font_open("sd:/ui16/font16.ui16");
    const Ui16MenuEntry entries[]={{3,u8"新天地"}};
    sd_reads=sd_seeks=0;
    ui16_send_system_menu(entries,1,0);
    assert(sd_reads==3 && sd_seeks==3);
    assert(count_command(0x12)==3 && count_command(0x13)==2 && count_command(0x14)==28 && count_command(0x0f)==0);
    validate_verified_rows();
    assert(depth==0);
    if(argc==3){
        std::ofstream dump(argv[2],std::ios::binary);
        assert(dump);
        dump.write(reinterpret_cast<const char*>(wire.data()),
                   static_cast<std::streamsize>(wire.size()));
        assert(dump.good());
    }
    sd_reads=sd_seeks=0;
    wire.clear();ui16_send_system_menu(entries,1,0);
    assert(sd_reads==0 && sd_seeks==0);
    assert(count_command(0x12)==0 && count_command(0x13)==0 && count_command(0x14)==28);
    validate_verified_rows();
    // Two new glyphs fit in unused RAM slots, so the old text remains visible
    // while the bitmap data is uploaded. No global BLANK/SHOW is needed.
    const Ui16MenuEntry next_entries[]={{3,u8"薔薇"}};
    wire.clear();ui16_send_system_menu(next_entries,1,0);
    assert(count_command(0x12)==2 && count_command(0x13)==0 && count_command(0x14)==28);
    validate_verified_rows();
    wire.clear();ui16_font_monitor_reset();ui16_send_system_menu(entries,1,0);
    assert(count_command(0x12)==3 && count_command(0x14)==28);
    validate_verified_rows();
    // A frame can use all 705 dynamic slots, but never recycle a slot already
    // allocated for the same frame (even if the file contains 7089 glyphs).
    ui16_font_open("sd:/ui16/font16.ui16");
    ui16_font_begin_frame();
    unsigned assigned=0,replaced=0;
    std::uint32_t first_dynamic=0,new_dynamic=0;
    std::size_t next_index=0;
    for(std::size_t n=0;n<7089 && assigned<705;++n){
        const std::size_t p=32+36*n;
        const std::uint32_t cp=std::uint32_t(image[p]) |
            (std::uint32_t(image[p+1])<<8) |
            (std::uint32_t(image[p+2])<<16) | (std::uint32_t(image[p+3])<<24);
        bool builtin=false;
        for(const auto &e:ui16_font_map)if(e.codepoint==cp){builtin=true;break;}
        if(builtin)continue;
        const auto slot=ui16_font_resolve(cp,0);
        if(slot>=319 && slot<1024){
            if(assigned==0)first_dynamic=cp;
            ++assigned;
            if(assigned==705)next_index=n+1;
        }
        else ++replaced;
    }
    assert(assigned==705 && replaced==0);
    wire.clear();
    assert(ui16_font_before_cells() && ui16_font_after_cells());
    assert(count_command(0x13)==2 && count_command(0x12)==705);
    for(std::size_t n=next_index;n<7089;++n){
        const std::size_t p=32+36*n;
        const std::uint32_t cp=std::uint32_t(image[p]) |
            (std::uint32_t(image[p+1])<<8) |
            (std::uint32_t(image[p+2])<<16) | (std::uint32_t(image[p+3])<<24);
        bool builtin=false;
        for(const auto &e:ui16_font_map)if(e.codepoint==cp){builtin=true;break;}
        if(!builtin){new_dynamic=cp;break;}
    }
    assert(first_dynamic && new_dynamic);
    ui16_font_begin_frame();
    ui16_font_pin_existing(true);
    assert(ui16_font_resolve(first_dynamic,0)==319);
    ui16_font_pin_existing(false);
    assert(ui16_font_resolve(new_dynamic,0)!=319);
    assert(ui16_font_resolve(first_dynamic,0)==319);
    wire.clear();
    assert(ui16_font_before_cells() && ui16_font_after_cells());
    assert(count_command(0x13)==2 && count_command(0x12)==1);
    // Exercise 1,152 distinct microSD glyphs in 64 short frames, then walk
    // back. The cache exceeds 705 slots, yet the previous frame occupies only
    // 18 slots. Every replacement can use a hidden slot without global BLANK.
    std::vector<std::uint32_t> distinct;
    for(std::size_t n=0;n<7089 && distinct.size()<1152;++n){
        const std::size_t p=32+36*n;
        const auto cp=std::uint32_t(image[p]) |
            (std::uint32_t(image[p+1])<<8) |
            (std::uint32_t(image[p+2])<<16) | (std::uint32_t(image[p+3])<<24);
        bool builtin=false;
        for(const auto &e:ui16_font_map)if(e.codepoint==cp){builtin=true;break;}
        if(!builtin)distinct.push_back(cp);
    }
    assert(distinct.size()==1152);
    ui16_font_open("sd:/ui16/font16.ui16");
    for(int sweep=0;sweep<2;++sweep)for(int step=0;step<64;++step){
        const int frame=sweep==0?step:63-step;
        ui16_font_begin_frame();
        for(int i=0;i<18;++i)
            assert(ui16_font_resolve(distinct[frame*18+i],0)>=319);
        wire.clear();
        assert(ui16_font_before_cells() && ui16_font_after_cells());
        assert(count_command(0x13)==(sweep==0 && step==0?2:0));
    }
    // All glyph data is read before any required BLANK; a read fault cannot
    // leave the display hidden halfway through a frame.
    ui16_font_open("sd:/ui16/font16.ui16");
    fail_on_blank=true;
    wire.clear();ui16_send_system_menu(entries,1,0);
    assert(!fail_on_blank && fail_next_read);
    fail_on_blank=false;fail_next_read=false;
    assert(count_command(0x13)==2 && count_command(0x12)==3 && count_command(0x14)==28);
    validate_verified_rows();
    // Inject a read failure while resolving a new glyph. Fallback is sent
    // before BLANK, so a half-updated font never reaches the display.
    ui16_font_open("sd:/ui16/font16.ui16");
    fail_next_read=true;
    wire.clear();ui16_send_system_menu(entries,1,0);
    assert(!fail_next_read);
    assert(count_command(0x13)==0 && count_command(0x0f)==28);
    // Missing or corrupt file must preserve original 1988-byte system UI.
    image.back()^=0x20;
    ui16_font_open("sd:/ui16/font16.ui16");
    wire.clear();ui16_send_system_menu(entries,1,0);
    assert(wire.size()==1988 && count_command(0x12)==0 && count_command(0x13)==0 && count_command(0x0f)==28);
    std::puts("UI16 font menu: CRC8 rows, RAM glyph cache, FPGA reload, pre-BLANK I/O fault and corrupt pack fallback OK");
}
