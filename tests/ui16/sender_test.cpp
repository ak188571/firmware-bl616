#include "ui16_cell.h"
#include "ui16_font_map.h"
#include "utils.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int critical_depth=0;
static unsigned entered=0;
static std::vector<unsigned> bytes;
void enter_critical(){assert(critical_depth==0);++critical_depth;++entered;}
void leave_critical(){assert(critical_depth==1);--critical_depth;}
void fpga_tx_byte(std::uint8_t b){assert(critical_depth==1);bytes.push_back(b);}
void fpga_tx_header(int cmd,int len){fpga_tx_byte(0xAA);fpga_tx_byte(len>>8);fpga_tx_byte(len);fpga_tx_byte(cmd);}
extern void ui16_send_selftest();
int main(){
    ui16_send_selftest();assert(bytes.size()==994&&critical_depth==0&&entered==14);
    std::ifstream f("../monitor/src/iosys/ui16/cells_selftest.hex");assert(f);
    std::vector<unsigned> cells;std::string word;while(f>>word)cells.push_back(std::stoul(word,nullptr,16));
    assert(cells.size()==512);
    std::size_t pos=0;
    for(unsigned y=0;y<14;++y){
        for(auto b:std::vector<unsigned>{0xAA,0,68,15,y,0,32})assert(bytes[pos++]==b);
        for(unsigned x=0;x<32;++x){auto c=cells[y*32+x];assert(bytes[pos++]==(c&255));assert(bytes[pos++]==ui16_wire_high(c));}
    }
    std::puts("BL616 sender host test passed: 994 bytes match FPGA selftest; 14 atomic transmit sections");
}
