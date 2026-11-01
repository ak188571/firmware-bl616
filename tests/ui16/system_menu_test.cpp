#include "ui16_cell.h"
#include "ui16_font_map.h"
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

static unsigned gid(unsigned cp){for(auto e:ui16_font_map)if(e.codepoint==cp)return e.glyph;return 0;}
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
        assert(cells[13*32+2+i*2] == (nes[i]|UI16_SELECTED));
        assert(cells[13*32+3+i*2] == (nes[i]|UI16_SELECTED|UI16_HALF));
        assert(cells[14*32+2+i*2] == (nes[i]|UI16_SELECTED|UI16_VERTICAL_HALF));
    }
    for (unsigned i=0; i<sizeof(snes)/sizeof(*snes); ++i) {
        assert(cells[16*32+2+i*2] == snes[i]);
        assert(cells[16*32+3+i*2] == (snes[i]|UI16_HALF));
    }
    for (int i=0; i<CELL_COUNT; ++i) {
        if (i/32 != 13 && i/32 != 14) assert((cells[i]&UI16_SELECTED) == 0);
    }
    for(int y=UI16_MENU_FIRST_ROW;y<13;++y)
        for(int x=0;x<32;++x)assert(cells[y*32+x]==0); // No last item above the first.
    ui16_build_system_menu(entries,8,cells,7);
    for(int y=15;y<UI16_MENU_END_ROW;++y)
        for(int x=0;x<32;++x)assert(cells[y*32+x]==0); // No first item below the last.
    ui16_build_system_menu(entries,8,cells);
    ui16_send_system_menu(entries, 8);
    assert(bytes.size() == 1988 && sections == 28 && critical_depth == 0);
    std::size_t pos=0;
    for(int y=0; y<MENU_ROWS; ++y) {
        for (const unsigned b : {0xaa,0,68,15,y,0,32}) assert(bytes[pos++] == b);
        for(int x=0; x<32; ++x) {
            assert(bytes[pos++] == (cells[y*32+x]&255));
            assert(bytes[pos++] == ui16_wire_high(cells[y*32+x]));
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
    assert(cells[10*32+2]==75 && cells[13*32+2]==(11|UI16_SELECTED));
    assert(cells[16*32+2]==1);
    Ui16MenuEntry short_list[8];for(auto &entry:short_list)entry={3,"A"};
    ui16_build_system_menu(short_list,7,cells,0);
    assert(cells[4*32+2]==(gid('A')|UI16_SELECTED));
    assert(cells[22*32+2]==gid('A'));
    ui16_build_system_menu(short_list,7,cells,6);
    assert(cells[4*32+2]==gid('A'));
    assert(cells[22*32+2]==(gid('A')|UI16_SELECTED));
    ui16_build_system_menu(short_list,8,cells,0);
    assert(cells[13*32+2]==(gid('A')|UI16_SELECTED));
    ui16_build_system_menu(short_list,8,cells,7);
    assert(cells[13*32+2]==(gid('A')|UI16_SELECTED));
    Ui16GameList short_games;short_games.status=Ui16ListStatus::Ready;
    short_games.names={"A","B","C"};
    ui16_build_game_menu("NES",short_games,0,cells);
    assert(cells[10*32+2]==(gid('A')|UI16_SELECTED));
    ui16_build_game_menu("NES",short_games,2,cells);
    assert(cells[10*32+2]==gid('A'));
    assert(cells[16*32+2]==(gid('C')|UI16_SELECTED));
    // Empty lists clear all previous rows, keeping only the heading.
    ui16_build_system_menu(nullptr,0,cells);
    for(int y=2; y<MENU_ROWS; ++y) for(int x=0; x<32; ++x) assert(cells[y*32+x]==0);
    // Truncation must not write past the screen or split a wide character.
    struct Guarded { std::uint16_t pre=0x1234, data[CELL_COUNT], post=0x5678; } guarded;
    Ui16MenuEntry many[40]; for(auto &e:many) e={3,u8"AAAAAAAAAAAAAAAAAAAAAAAAAAAAA日"};
    ui16_build_system_menu(many,40,guarded.data);
    assert(guarded.pre==0x1234 && guarded.post==0x5678);
    assert((guarded.data[13*32+30]&255)==62 && guarded.data[13*32+31]==0);
    for (int row : {2,25,26,27})
        for (int x=0; x<32; ++x) assert(guarded.data[row*32+x]==0);
    // Invalid/truncated UTF-8 and missing glyphs remain blank and terminate safely.
    const Ui16MenuEntry bad[]={{3,"A\xe3\x81"},{3,"\xf0\x9f\x98\x80"},{3,"A\xed\xa0\x80"}};
    ui16_build_system_menu(bad,3,cells);
    assert((cells[10*32+2]&4095)==62 && (cells[10*32+3]&4095)==gid(0x25a1));
    assert((cells[13*32+2]&4095)==gid(0x25a1));
    ui16_build_starting_game(cells);
    assert((cells[13*32+8]&4095)==gid('S') && (cells[13*32+23]&4095)==gid('.'));
    for(int i=0;i<CELL_COUNT;++i)if(i/32!=13&&i/32!=14)assert(cells[i]==0);
    bytes.clear();sections=0;ui16_send_starting_game();
    assert(bytes.size()==1988&&sections==28&&critical_depth==0);
    std::puts("UI16 system menu tests passed: layout, selection, configuration order, UTF-8, clipping, clear, 1988-byte sender");
}
