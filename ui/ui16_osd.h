#pragma once
#include <array>
#include <vector>
#include <cstdint>

// Game OSD uses the existing 8x8 font. Position is retained until power-off.
enum class Ui16OsdPosition { Center, TopLeft, TopRight, BottomLeft, BottomRight };
struct Ui16OsdCanvas {
    static constexpr int Rows=128;
    std::array<char,32*Rows> cells{};
    int x=0,y=0;
};
struct Ui16OsdFrame {
    std::array<char,32*28> cells{};
    unsigned x=0,y=0,width=0,height=0;
};
const char *ui16_osd_position_name(Ui16OsdPosition);
Ui16OsdFrame ui16_osd_layout(const Ui16OsdCanvas &,const std::vector<int> &options,
    int selected,int &top,int &width_floor,Ui16OsdPosition position);
void ui16_osd_capture_begin();
void ui16_osd_capture_end();
bool ui16_osd_capture_active();
bool ui16_osd_capture_clear();
bool ui16_osd_capture_cursor(int x,int y);
bool ui16_osd_capture_text(const char *text);
const Ui16OsdCanvas &ui16_osd_canvas();
