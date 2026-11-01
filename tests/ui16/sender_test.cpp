#include "ui16_cell.h"
#include "ui16_font_map.h"
#include "ui16_selftest_cells.h"
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
    ui16_send_selftest();assert(bytes.size()==1988&&critical_depth==0&&entered==28);
    // The UART fixture and FPGA power-on fixture are independent test patterns.
    static_assert(sizeof(ui16_selftest_cells)/sizeof(*ui16_selftest_cells)==448);
    std::size_t pos=0;
    for(unsigned y=0;y<28;++y){
        for(auto b:std::vector<unsigned>{0xAA,0,68,15,y,0,32})assert(bytes[pos++]==b);
        for(unsigned x=0;x<32;++x){auto c=ui16_selftest_cells[(y/2)*32+x];if(y&1)c|=UI16_VERTICAL_HALF;assert(bytes[pos++]==(c&255));assert(bytes[pos++]==ui16_wire_high(c));}
    }
    std::puts("BL616 sender host test passed: 1988 bytes with vertical halves; 28 atomic transmit sections");
}
