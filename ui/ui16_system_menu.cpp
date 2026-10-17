#ifdef UI16_SYSTEM_MENU
#include "ui16_system_menu.h"
#include "ui16_font_map.h"
#include "ui16_cell.h"
#include "utils.h"
#include <string_view>
#include <algorithm>
#include <limits>

namespace {
// Same UTF-8 validation and cell widths as the PC simulator.
std::uint32_t next_utf8(std::string_view s, std::size_t &pos)
{
    const auto first = static_cast<unsigned char>(s[pos]);
    if (first < 0x80) { ++pos; return first; }
    const int n = first >= 0xc2 && first <= 0xdf ? 2 :
                  first >= 0xe0 && first <= 0xef ? 3 :
                  first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (!n || pos + n > s.size()) { ++pos; return 0xfffd; }
    std::uint32_t cp = first & (0x7f >> n);
    for (int i = 1; i < n; ++i) {
        const auto c = static_cast<unsigned char>(s[pos + i]);
        if ((c & 0xc0) != 0x80) { ++pos; return 0xfffd; }
        cp = (cp << 6) | (c & 0x3f);
    }
    if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) ||
        (n == 4 && cp < 0x10000) || (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) {
        ++pos; return 0xfffd;
    }
    pos += n;
    return cp;
}

void text(std::uint16_t *row, const char *label, bool selected)
{
    if (!label) return;
    const std::string_view s(label);
    int x = 2;
    std::size_t pos = 0;
    while (pos < s.size()) {
        auto cp = next_utf8(s, pos);
        // The prototype font contains uppercase Latin letters only.
        // ASCII case and punctuation are preserved by the expanded font.
        const int width = cp < 0x80 ? 1 : 2;
        if (x + width > TEXT_UNITS_PER_ROW) break; // Never split a fullwidth glyph.
        std::uint16_t glyph = 0;
        bool found = cp == 32;
        for (const auto &g : ui16_font_map)
            if (g.codepoint == cp) { glyph = g.glyph; found = true; break; }
        if (!found) {
            const auto fallback = cp < 128 ? 0x003fu : 0x25a1u;
            for (const auto &g : ui16_font_map) if (g.codepoint == fallback) { glyph=g.glyph; break; }
        }
        const std::uint16_t attr = selected ? 1u << (12 + ATTR_SELECTED_BIT) : 0;
        row[x++] = glyph | attr;
        if (width == 2) row[x++] = glyph | attr | (1u << (12 + ATTR_HALF_BIT));
    }
}

}

const char *ui16_system_label(const Ui16MenuEntry &entry)
{
    switch (entry.id) {
    case 1: return u8"ファミリーコンピュータ";
    case 2: return u8"スーパーファミコン";
    default: return entry.name;
    }
}

int ui16_scroll_top(int selected, std::size_t count, int top)
{
    const int n=static_cast<int>(std::min<std::size_t>(count,std::numeric_limits<int>::max()));
    const int max_top=std::max(0,n-UI16_MENU_CAPACITY);
    top=std::max(0,std::min(top,max_top));
    if(selected>=0 && selected<n) {
        if(selected<top)top=selected;
        else if(selected>=top+UI16_MENU_CAPACITY)top=selected-UI16_MENU_CAPACITY+1;
    }
    return std::min(top,max_top);
}

void ui16_build_system_menu(const Ui16MenuEntry *entries, std::size_t count,
                           std::uint16_t (&cells)[CELL_COUNT], int selected_index, int top)
{
    for (auto &cell : cells) cell = 0;
    text(cells + TEXT_UNITS_PER_ROW, "SYSTEMS", false);
    std::size_t actual=0;
    while(entries && actual<count && entries[actual].id!=0)++actual;
    if(top<0)top=ui16_scroll_top(selected_index,actual,0);
    else top=ui16_scroll_top(-1,actual,top);
    bool selected = false;
    int row = UI16_MENU_FIRST_ROW;
    for (std::size_t i = static_cast<std::size_t>(top); entries && i < actual && row < MENU_ROWS - 1; ++i) {
        if (entries[i].id == 0) break;
        const bool select = selected_index == -1 ? entries[i].id > 0 && !selected :
                            selected_index >= 0 && i == static_cast<std::size_t>(selected_index);
        selected |= select;
        text(cells + row++ * TEXT_UNITS_PER_ROW, ui16_system_label(entries[i]), select);
    }
}

static void send_cells(const std::uint16_t (&cells)[CELL_COUNT])
{
    for (int y = 0; y < MENU_ROWS; ++y) {
        taskENTER_CRITICAL();
        fpga_tx_header(ROW_COMMAND, 4 + 2 * TEXT_UNITS_PER_ROW);
        fpga_tx_byte(static_cast<std::uint8_t>(y));
        fpga_tx_byte(0);
        fpga_tx_byte(TEXT_UNITS_PER_ROW);
        for (int x = 0; x < TEXT_UNITS_PER_ROW; ++x) {
            const auto cell = cells[y * TEXT_UNITS_PER_ROW + x];
            fpga_tx_byte(static_cast<std::uint8_t>(cell & 255));
            fpga_tx_byte(static_cast<std::uint8_t>(ui16_wire_high(cell)));
        }
        taskEXIT_CRITICAL();
    }
}

void ui16_send_system_menu(const Ui16MenuEntry *entries, std::size_t count, int selected_index, int top)
{
    std::uint16_t cells[CELL_COUNT];
    ui16_build_system_menu(entries, count, cells, selected_index, top);
    send_cells(cells);
}

void ui16_build_game_menu(const char *heading, const Ui16GameList &games, int selected,
                         std::uint16_t (&cells)[CELL_COUNT], int top)
{
    for (auto &cell : cells) cell = 0;
    text(cells + TEXT_UNITS_PER_ROW, heading, false);
    top=ui16_scroll_top(top<0?selected:-1,games.names.size(),std::max(0,top));
    if (games.status == Ui16ListStatus::Ready) {
        for (std::size_t i = static_cast<std::size_t>(top); i < games.names.size() && i < static_cast<std::size_t>(top+UI16_MENU_CAPACITY); ++i) {
            std::string display=i < games.titles.size() && !games.titles[i].empty() ? games.titles[i] : games.names[i];
            if(i<games.directories.size() && games.directories[i])display+="/";
            text(cells + (UI16_MENU_FIRST_ROW + i - top) * TEXT_UNITS_PER_ROW,
                 display.c_str(), selected >= 0 && i == static_cast<std::size_t>(selected));
        }
    } else {
        const char *message = games.status == Ui16ListStatus::Empty ? "EMPTY FOLDER" :
                              games.status == Ui16ListStatus::DirectoryError ? "DIRECTORY ERROR" : "READ ERROR";
        text(cells + UI16_MENU_FIRST_ROW * TEXT_UNITS_PER_ROW, message, false);
    }
    text(cells + (MENU_ROWS - 1) * TEXT_UNITS_PER_ROW,
         games.navigation_error ? u8"X/Y 戻る NAV ERROR" : games.titles_error ? u8"X/Y 戻る CSV ERROR" : games.more ? u8"X/Y 戻る LIST LIMIT" : u8"X/Y 戻る", false);
}

void ui16_send_game_menu(const char *heading, const Ui16GameList &games, int selected, int top)
{
    std::uint16_t cells[CELL_COUNT];
    ui16_build_game_menu(heading, games, selected, cells, top);
    send_cells(cells);
}
#endif
