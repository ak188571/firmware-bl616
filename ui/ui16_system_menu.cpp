#ifdef UI16_SYSTEM_MENU
#include "ui16_system_menu.h"
#include "ui16_font_map.h"
#include "ui16_cell.h"
#include "utils.h"
#include <string_view>
#include <algorithm>
#include <limits>

namespace {
void text(std::uint16_t *row, const char *label, bool selected, int x = 2)
{
    if (!label) return;
    const std::string_view s(label);
    std::size_t pos = 0;
    while (pos < s.size()) {
        auto cp = ui16_next_utf8(s, pos);
        if(cp==10)break;
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

std::string ui16_game_label(const Ui16GameList &games, std::size_t index)
{
    std::string label=index<games.titles.size()&&!games.titles[index].empty()?games.titles[index]:games.names[index];
    if(index<games.directories.size()&&games.directories[index])label+="/";
    return label;
}

int ui16_scroll_top(int selected, std::size_t count, int top)
{
    return ui16_variable_scroll_top(selected,static_cast<int>(std::min<std::size_t>(count,std::numeric_limits<int>::max())),top,[](int){return 1;});
}
int ui16_system_scroll_top(const Ui16MenuEntry *entries, std::size_t count, int selected, int top)
{
    int n=0;while(entries&&static_cast<std::size_t>(n)<count&&entries[n].id!=0)++n;
    return ui16_variable_scroll_top(selected,n,top,[&](int i){return ui16_layout_text(ui16_system_label(entries[i])).count;});
}
int ui16_game_scroll_top(const Ui16GameList &games, int selected, int top)
{
    return ui16_variable_scroll_top(selected,static_cast<int>(games.names.size()),top,[&](int i){return ui16_layout_text(ui16_game_label(games,i)).count;});
}
static int draw_item(std::uint16_t *cells,int row,const std::string &label,bool selected)
{
    const auto layout=ui16_layout_text(label);
    if(row+layout.count>MENU_ROWS-1)return 0;
    for(int line=0;line<layout.count;++line){
        const auto part=std::string(layout.lines[line]);
        text(cells+(row+line)*TEXT_UNITS_PER_ROW,part.c_str(),selected,line?3:2);
        if(line&&selected)cells[(row+line)*TEXT_UNITS_PER_ROW+2]=1u<<(12+ATTR_SELECTED_BIT);
    }
    return layout.count;
}

void ui16_build_system_menu(const Ui16MenuEntry *entries, std::size_t count,
                           std::uint16_t (&cells)[CELL_COUNT], int selected_index, int top)
{
    for (auto &cell : cells) cell = 0;
    text(cells + TEXT_UNITS_PER_ROW, "SYSTEMS", false);
    std::size_t actual=0;
    while(entries && actual<count && entries[actual].id!=0)++actual;
    top=ui16_system_scroll_top(entries,actual,top<0?selected_index:-1,std::max(0,top));
    bool selected = false;
    int row = UI16_MENU_FIRST_ROW;
    for (std::size_t i = static_cast<std::size_t>(top); entries && i < actual && row < MENU_ROWS - 1; ++i) {
        if (entries[i].id == 0) break;
        const bool select = selected_index == -1 ? entries[i].id > 0 && !selected :
                            selected_index >= 0 && i == static_cast<std::size_t>(selected_index);
        selected |= select;
        const int height=draw_item(cells,row,ui16_system_label(entries[i]),select);
        if(!height)break;
        row+=height;
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
    top=ui16_game_scroll_top(games,top<0?selected:-1,std::max(0,top));
    if (games.status == Ui16ListStatus::Ready) {
        int row=UI16_MENU_FIRST_ROW;
        for(std::size_t i=static_cast<std::size_t>(top);i<games.names.size();++i){
            const int height=draw_item(cells,row,ui16_game_label(games,i),selected>=0&&i==static_cast<std::size_t>(selected));
            if(!height)break;
            row+=height;
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
