#include "ui16_font_sender.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
static int depth=0;
static std::vector<unsigned> stream;
void enter_critical(){assert(depth++==0);}
void leave_critical(){assert(--depth==0);}
void fpga_tx_byte(std::uint8_t b){assert(depth==1);stream.push_back(b);}
void fpga_tx_header(int cmd,int len){
    fpga_tx_byte(0xaa);fpga_tx_byte(len>>8);fpga_tx_byte(len);fpga_tx_byte(cmd);
}
static std::uint8_t crc(std::vector<unsigned> bytes){
    unsigned c=0;
    for(auto b:bytes){c^=b;for(int j=0;j<8;++j)c=(c&128)?((c<<1)^7)&255:(c<<1)&255;}
    return c;
}
int main(){
    std::uint8_t data[32];for(int i=0;i<32;++i)data[i]=std::uint8_t(i*7);
    ui16_font_blank();
    ui16_font_write(1023,data);
    ui16_font_show();
    assert(depth==0 && stream.size()==6+39+6);
    assert((std::vector<unsigned>(stream.begin(),stream.begin()+6)==std::vector<unsigned>{0xaa,0,3,0x13,0,0}));
    assert((std::vector<unsigned>(stream.begin()+6,stream.begin()+12)==std::vector<unsigned>{0xaa,0,36,0x12,255,3}));
    for(int j=0;j<32;++j)assert(stream[12+j]==data[j]);
    assert(stream[44]==crc(std::vector<unsigned>(stream.begin()+10,stream.begin()+44)));
    assert((std::vector<unsigned>(stream.begin()+45,stream.end())==std::vector<unsigned>{0xaa,0,3,0x13,1,7}));
    const auto size=stream.size();ui16_font_write(1024,data);assert(stream.size()==size);
    std::puts("BL616 font writer: gate, 36-byte glyph, CRC8, slot bounds OK");
}
