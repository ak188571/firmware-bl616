#include "ui16_system_menu.h"
#include "utils.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>

int critical_depth = 0;
static int sections = 0;
static std::vector<unsigned char> bytes;
void enter_critical() { assert(critical_depth == 0); ++critical_depth; ++sections; }
void leave_critical() { assert(critical_depth == 1); --critical_depth; }
void fpga_tx_byte(std::uint8_t byte) { assert(critical_depth == 1); bytes.push_back(byte); }
void fpga_tx_header(int cmd, int len) {
    fpga_tx_byte(0xaa); fpga_tx_byte(len >> 8); fpga_tx_byte(len); fpga_tx_byte(cmd);
}

int main()
{
    const Ui16MenuEntry entries[] = {
        {1,"NES"}, {2,"SNES"}, {3,"Game Boy Advance"},
        {4,"MegaDrive / Genesis"}, {5,"Sega Master System"},
        {6,"IBM PC/XT"}, {-1,"CORES"}, {-2,"OPTIONS"}
    };
    std::uint16_t cells[CELL_COUNT];
    ui16_build_system_menu(entries, 8, cells);
    // Known FPGA glyph IDs; Japanese characters must occupy paired left/right cells.
    const int nes[] = {1,2,3,4,5,6,7,8,9,5,10};
    const int snes[] = {11,5,12,5,1,2,3,6,7};
    for (unsigned i=0; i<sizeof(nes)/sizeof(*nes); ++i) {
        assert(cells[3*32+2+i*2] == (nes[i]|256));
        assert(cells[3*32+3+i*2] == (nes[i]|768));
    }
    for (unsigned i=0; i<sizeof(snes)/sizeof(*snes); ++i) {
        assert(cells[4*32+2+i*2] == snes[i]);
        assert(cells[4*32+3+i*2] == (snes[i]|512));
    }
    assert(cells[5*32+2] == 67); // G, Game Boy Advance
    assert(cells[5*32+3] == 62); // A (lowercase was converted)
    assert(cells[6*32+2] == 73); // M, MegaDrive
    assert(cells[7*32+2] == 111); // S, Sega Master System
    assert(cells[8*32+2] == 69); // I, IBM PC/XT
    assert(cells[9*32+2] == 64); // C, CORES
    assert(cells[10*32+2] == 75); // O, OPTIONS
    for (int i=0; i<CELL_COUNT; ++i) {
        if (i/32 != 3) assert((cells[i]&256) == 0);
        if (i/32 == 0 || i/32 == 2 || i/32 >= 11) assert(cells[i] == 0);
    }
    ui16_send_system_menu(entries, 8);
    assert(bytes.size() == 994 && sections == 14 && critical_depth == 0);
    std::size_t pos=0;
    for(int y=0; y<14; ++y) {
        for (const unsigned b : {0xaa,0,68,14,y,0,32}) assert(bytes[pos++] == b);
        for(int x=0; x<32; ++x) {
            assert(bytes[pos++] == (cells[y*32+x]&255));
            assert(bytes[pos++] == ((cells[y*32+x]>>8)&3));
        }
    }
    std::ofstream frame("build-ui16-host/system_menu_frame.bin", std::ios::binary);
    frame.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::ofstream hex("build-ui16-host/system_menu_cells.hex");
    char word[16];
    for (int i=0; i<512; ++i) { std::snprintf(word,sizeof(word),"%03x\n",i<CELL_COUNT?cells[i]:0); hex<<word; }

    // Reordered/subset configuration determines the selected first core.
    const Ui16MenuEntry subset[] = {{-2,"OPTIONS"},{2,"SNES"},{1,"NES"},{0,"END"},{3,"GBA"}};
    ui16_build_system_menu(subset,5,cells);
    assert(cells[3*32+2]==75 && cells[4*32+2]==(11|256));
    assert(cells[5*32+2]==1 && cells[6*32+2]==0);
    // Empty lists clear all previous rows, keeping only the heading.
    ui16_build_system_menu(nullptr,0,cells);
    for(int y=2; y<14; ++y) for(int x=0; x<32; ++x) assert(cells[y*32+x]==0);
    // Truncation must not write past the screen or split a wide character.
    struct Guarded { std::uint16_t pre=0x1234, data[CELL_COUNT], post=0x5678; } guarded;
    Ui16MenuEntry many[40]; for(auto &e:many) e={3,u8"AAAAAAAAAAAAAAAAAAAAAAAAAAAAA日"};
    ui16_build_system_menu(many,40,guarded.data);
    assert(guarded.pre==0x1234 && guarded.post==0x5678);
    assert((guarded.data[3*32+30]&255)==62 && guarded.data[3*32+31]==0);
    // Invalid/truncated UTF-8 and missing glyphs remain blank and terminate safely.
    const Ui16MenuEntry bad[]={{3,"A\xe3\x81"},{3,"\xf0\x9f\x98\x80"},{3,"A\xed\xa0\x80"}};
    ui16_build_system_menu(bad,3,cells);
    assert((cells[3*32+2]&255)==62 && (cells[3*32+3]&255)==0);
    assert(cells[4*32+2]==0 && cells[4*32+3]==512);
    std::puts("UI16 system menu tests passed: layout, selection, configuration order, UTF-8, clipping, clear, 994-byte sender");
}
