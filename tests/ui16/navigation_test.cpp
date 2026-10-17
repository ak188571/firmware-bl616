#include "ui16_cell.h"
#include "ui16_font_map.h"
#include "ui16_navigation.h"
#include "ui16_display_mode.h"
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
    constexpr auto up = UI16_BUTTON_UP, down = UI16_BUTTON_DOWN;
    Ui16Navigation nav(8);
    assert(nav.selected() == 0);
    assert(!nav.update(0, 0));
    assert(nav.update(down, 10) && nav.selected() == 1);
    assert(!nav.update(down, 20));
    assert(!nav.update(0, 30));
    assert(nav.update(down, 40) && nav.selected() == 2); // short second tap
    assert(!nav.update(down, 439));
    assert(nav.update(down, 440) && nav.selected() == 3); // 400 ms first repeat
    assert(!nav.update(down, 539));
    assert(nav.update(down, 540) && nav.selected() == 4); // 100 ms subsequent repeat
    assert(!nav.update(0, 541));
    assert(!nav.update(0, 10000) && nav.selected() == 4); // release stops repeating
    assert(nav.update(up, 10010) && nav.selected() == 3);
    assert(nav.update(down, 10020) && nav.selected() == 4); // direct reversal
    assert(!nav.update(up|down, 10030));
    assert(!nav.update(up|down, 20000) && nav.selected() == 4);
    assert(nav.update(up, 20001) && nav.selected() == 3);
    // Buttons A/B, Start, Select, Left/Right must not move or confirm anything.
    assert(!nav.update(0x3fcf, 20010) && nav.selected() == 3);
    // Exactly one move after a stall; no burst of accumulated repeats.
    assert(nav.update(down, 21000) && nav.selected() == 4);
    assert(nav.update(down, 1000000) && nav.selected() == 5);
    assert(!nav.update(down, 1000001));

    Ui16Navigation edge(8);
    assert(!edge.update(up, 0) && edge.selected() == 0);
    assert(!edge.update(up, 400));
    assert(edge.update(down, 401) && edge.selected() == 1);
    for (int i=0; i<6; ++i) assert(edge.update(down, 801+i*100));
    assert(edge.selected() == 7);
    assert(!edge.update(down, 1401) && edge.selected() == 7);
    assert(edge.update(up, 1402) && edge.selected() == 6);
    Ui16Navigation empty(0), single(1), cap(999,999), negative(8,-10);
    assert(empty.selected() == -1 && !empty.update(down, 0) && !empty.update(up, 1));
    assert(single.selected() == 0 && !single.update(up, 0) && !single.update(down, 1));
    assert(cap.selected() == UI16_MENU_CAPACITY-1 && negative.selected() == 0);
    Ui16Navigation wrapped(8);
    const std::uint32_t start = 0xffffff00;
    assert(wrapped.update(down,start));
    assert(!wrapped.update(down,start+399));
    assert(wrapped.update(down,start+400));
    assert(!wrapped.update(down,start+499));
    assert(wrapped.update(down,start+500) && wrapped.selected() == 3);
    // Opposite directions from separate controllers are cancelled by the OR merge.
    Ui16Navigation mixed(8,3);
    assert(!mixed.update(up|down, 0) && mixed.selected()==3);

    const Ui16MenuEntry entries[] = {
        {1,"NES"},{2,"SNES"},{3,"Game Boy Advance"},{4,"MegaDrive / Genesis"},
        {5,"Sega Master System"},{6,"IBM PC/XT"},{-1,"CORES"},{-2,"OPTIONS"}
    };
    std::uint16_t baseline[CELL_COUNT], cells[CELL_COUNT];
    ui16_build_system_menu(entries,8,baseline,0);
    for (int selected=0; selected<8; ++selected) {
        ui16_build_system_menu(entries,8,cells,selected);
        for (int i=0; i<CELL_COUNT; ++i) {
            assert((cells[i]&~4096)==(baseline[i]&~4096)); // no stale/mutated glyphs
            if (i/32 != UI16_MENU_FIRST_ROW+selected) assert((cells[i]&4096)==0);
        }
        assert(cells[(UI16_MENU_FIRST_ROW+selected)*32+2]&4096); // includes CORES/OPTIONS
        bytes.clear(); sections=0;
        ui16_send_system_menu(entries,8,selected);
        assert(bytes.size()==994 && sections==14 && critical_depth==0);
        std::size_t pos=0;
        for (int y=0; y<14; ++y) {
            for (int b : {0xaa,0,68,15,y,0,32}) assert(bytes[pos++]==b);
            for (int x=0; x<32; ++x) {
                assert(bytes[pos++]==(cells[y*32+x]&255));
                assert(bytes[pos++]==ui16_wire_high(cells[y*32+x]));
            }
        }
        char path[100], word[16];
        std::snprintf(path,sizeof(path),"build-ui16-host/selected_%d.hex",selected);
        std::ofstream hex(path);
        for (int i=0; i<512; ++i) {
            std::snprintf(word,sizeof(word),"%03x\n",i<CELL_COUNT?cells[i]:0); hex<<word;
        }
    }
    ui16_build_system_menu(entries,8,cells,999);
    for (auto c:cells) assert((c&4096)==0);
    // Ownership conversion never changes other protocol commands or the payload.
    for (int cmd=0; cmd<4096; ++cmd) {
        assert(ui16_display_command(cmd,false)==cmd);
        assert(ui16_display_command(cmd,true)==(cmd==5 ? 13 : cmd));
    }
    std::puts("Stage 2 tests passed: taps, repeat 400/100ms, release, reversal, bounds, simultaneous input, timer wrap, 8 selections, 994-byte frames, display ownership");
}
