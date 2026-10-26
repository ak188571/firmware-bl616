#ifdef UI16_SYSTEM_MENU
#include "ui16_system_menu.h"
#include "ui16_font_map.h"
#include "ui16_cell.h"
#ifdef UI16_SD_FONT
#include "ui16_font_cache.h"
#include "ui16_link.h"
#endif
#include "utils.h"
#include <string_view>
#include <algorithm>
#include <limits>

namespace {
#ifdef UI16_SD_FONT
bool use_sd_font = false;
#endif
void text(std::uint16_t *cells, int y, const char *label, bool selected, int x = 2)
{
    if (!label || y < 0 || y + GLYPH_VERTICAL_UNITS > MENU_ROWS) return;
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
#ifdef UI16_SD_FONT
        if (use_sd_font) glyph=ui16_font_resolve(cp,glyph);
#endif
        const std::uint16_t attr = selected ? 1u << (12 + ATTR_SELECTED_BIT) : 0;
        cells[y*TEXT_UNITS_PER_ROW+x] = glyph | attr;
        cells[(y+1)*TEXT_UNITS_PER_ROW+x++] = glyph | attr | UI16_VERTICAL_HALF;
        if (width == 2) {
            cells[y*TEXT_UNITS_PER_ROW+x] = glyph | attr | UI16_HALF;
            cells[(y+1)*TEXT_UNITS_PER_ROW+x++] = glyph | attr | UI16_HALF | UI16_VERTICAL_HALF;
        }
    }
}

struct VisibleItem { std::size_t index; int y; };
template<class Height> std::vector<VisibleItem> centered_layout(std::size_t count,int selected,Height height)
{
    std::vector<VisibleItem> result,above,below;
    if(!count||selected<0||static_cast<std::size_t>(selected)>=count)return result;
    int total=0;
    for(std::size_t index=0;index<count;++index){
        total+=height(index)+(index?ITEM_GAP_UNITS:0);
        if(total>UI16_MENU_CAPACITY)break;
    }
    if(total<=UI16_MENU_CAPACITY){
        int y=UI16_MENU_FIRST_ROW+(UI16_MENU_CAPACITY-total)/2;
        for(std::size_t index=0;index<count;++index){
            result.push_back({index,y});
            y+=height(index)+ITEM_GAP_UNITS;
        }
        return result;
    }
    const int selected_height=height(selected);
    const int selected_y=(MENU_ROWS-selected_height)/2;
    int above_cursor=selected_y-ITEM_GAP_UNITS;
    int below_cursor=selected_y+selected_height+ITEM_GAP_UNITS;
    const auto visible=[](int y,int h){
        for(int p=y;p<y+h;p+=GLYPH_VERTICAL_UNITS) {
            if(p>=UI16_MENU_FIRST_ROW&&p+GLYPH_VERTICAL_UNITS<=UI16_MENU_END_ROW)return true;
        }
        return false;
    };
    for(std::size_t index=static_cast<std::size_t>(selected);index>0;){
        --index;
        const int h=height(index),y=above_cursor-h;
        if(!visible(y,h))break;
        above.push_back({index,y});
        above_cursor=y-ITEM_GAP_UNITS;
    }
    for(std::size_t index=static_cast<std::size_t>(selected)+1;index<count;++index){
        const int h=height(index);
        if(!visible(below_cursor,h))break;
        below.push_back({index,below_cursor});
        below_cursor+=h+ITEM_GAP_UNITS;
    }
    for(auto it=above.rbegin();it!=above.rend();++it)result.push_back(*it);
    result.push_back({static_cast<std::size_t>(selected),selected_y});
    result.insert(result.end(),below.begin(),below.end());return result;
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
    for(int line=0;line<layout.count;++line){
        const auto part=std::string(layout.lines[line]);
        const int y=row+line*GLYPH_VERTICAL_UNITS;
        if(y<UI16_MENU_FIRST_ROW||y+GLYPH_VERTICAL_UNITS>UI16_MENU_END_ROW)continue;
        text(cells,y,part.c_str(),selected,line?3:2);
        if(line&&selected){
            cells[y*TEXT_UNITS_PER_ROW+2]=UI16_SELECTED;
            cells[(y+1)*TEXT_UNITS_PER_ROW+2]=UI16_SELECTED|UI16_VERTICAL_HALF;
        }
    }
    return layout.count*GLYPH_VERTICAL_UNITS;
}

void ui16_build_system_menu(const Ui16MenuEntry *entries, std::size_t count,
                           std::uint16_t (&cells)[CELL_COUNT], int selected_index, int top)
{
    for (auto &cell : cells) cell = 0;
    std::size_t actual=0;
    while(entries && actual<count && entries[actual].id!=0)++actual;
    int selected=selected_index;
    if(selected==-1)for(std::size_t i=0;i<actual;++i)if(entries[i].id>0){selected=static_cast<int>(i);break;}
    const int anchor=selected>=0&&static_cast<std::size_t>(selected)<actual?selected:(actual?0:-1);
    const auto layout=centered_layout(actual,anchor,[&](std::size_t i){return ui16_layout_text(ui16_system_label(entries[i])).count*GLYPH_VERTICAL_UNITS;});
    for(const auto &item:layout)draw_item(cells,item.y,ui16_system_label(entries[item.index]),static_cast<int>(item.index)==selected);
    text(cells,0,"SYSTEMS",false);
    (void)top;
}

static bool send_cells(const std::uint16_t (&cells)[CELL_COUNT],bool force_raw=false)
{
#ifndef UI16_RELIABLE_FONT
    (void)force_raw;
#endif
#ifdef UI16_SD_FONT
    const bool verified = ui16_font_frame_gated();
    if(force_raw && verified)ui16_font_start_raw_recovery();
#else
    constexpr bool verified = false;
#endif
    for (int y = 0; y < MENU_ROWS; ++y) {
        std::uint8_t bytes[3+2*TEXT_UNITS_PER_ROW+1]{};
        unsigned n=0;
        std::uint8_t crc = 0;
        const auto send = [&](std::uint8_t b) {
            bytes[n++]=b;
            if (verified) {
                crc ^= b;
                for (int i=0;i<8;++i)
                    crc = (crc & 0x80) ? std::uint8_t((crc << 1)^7) : std::uint8_t(crc << 1);
            }
        };
        send(static_cast<std::uint8_t>(y));
        send(0);
        send(TEXT_UNITS_PER_ROW);
        for (int x = 0; x < TEXT_UNITS_PER_ROW; ++x) {
            const auto cell = cells[y * TEXT_UNITS_PER_ROW + x];
            send(static_cast<std::uint8_t>(cell & 255));
            send(static_cast<std::uint8_t>(ui16_wire_high(cell)));
        }
        if (verified)bytes[n++]=crc;
#ifdef UI16_RELIABLE_FONT
        if (verified && !force_raw && !ui16_font_raw_recovery()) {
            if(!ui16_link_send(0x14,bytes,n))return false;
            continue;
        }
#endif
        taskENTER_CRITICAL();
        fpga_tx_header(verified ? 0x14 : ROW_COMMAND,
                       (verified ? 5 : 4) + 2 * TEXT_UNITS_PER_ROW);
        for(unsigned i=0;i<n;++i)fpga_tx_byte(bytes[i]);
        taskEXIT_CRITICAL();
    }
    return true;
}

void ui16_send_system_menu(const Ui16MenuEntry *entries, std::size_t count, int selected_index, int top)
{
    std::uint16_t cells[CELL_COUNT];
#ifdef UI16_SD_FONT
    ui16_font_begin_frame(); use_sd_font=true; ui16_font_pin_existing(true);
    ui16_build_system_menu(entries, count, cells, selected_index, top);
    ui16_font_pin_existing(false);
#endif
    ui16_build_system_menu(entries, count, cells, selected_index, top);
#ifdef UI16_SD_FONT
    use_sd_font=false;
    if (!ui16_font_before_cells()) ui16_build_system_menu(entries, count, cells, selected_index, top);
#endif
#ifdef UI16_SD_FONT
    if(!send_cells(cells,ui16_font_raw_recovery())) {
        ui16_font_abort_frame();
        ui16_build_system_menu(entries,count,cells,selected_index,top);
        (void)send_cells(cells,true);
    }
    if(!ui16_font_after_cells()) {
        ui16_font_abort_frame();
        ui16_build_system_menu(entries,count,cells,selected_index,top);
        (void)send_cells(cells,true);
        (void)ui16_font_after_cells();
    }
#else
    (void)send_cells(cells);
#endif
}

void ui16_build_game_menu(const char *heading, const Ui16GameList &games, int selected,
                         std::uint16_t (&cells)[CELL_COUNT], int top)
{
    for (auto &cell : cells) cell = 0;
    if (games.status == Ui16ListStatus::Ready) {
        std::vector<std::string> labels;labels.reserve(games.names.size());
        for(std::size_t i=0;i<games.names.size();++i)labels.push_back(ui16_game_label(games,i));
        const int anchor=selected>=0&&static_cast<std::size_t>(selected)<labels.size()?selected:(labels.empty()?-1:0);
        const auto layout=centered_layout(labels.size(),anchor,[&](std::size_t i){return ui16_layout_text(labels[i]).count*GLYPH_VERTICAL_UNITS;});
        for(const auto &item:layout)draw_item(cells,item.y,labels[item.index],static_cast<int>(item.index)==selected);
    } else {
        const char *message = games.status == Ui16ListStatus::Empty ? "EMPTY FOLDER" :
                              games.status == Ui16ListStatus::DirectoryError ? "DIRECTORY ERROR" : "READ ERROR";
        text(cells,(MENU_ROWS-GLYPH_VERTICAL_UNITS)/2,message,false);
    }
    const std::string footer=games.launch_error?std::string(u8"X/Y 戻る ")+games.launch_error:
        games.navigation_error?u8"X/Y 戻る NAV ERROR":games.titles_error?u8"X/Y 戻る CSV ERROR":games.more?u8"X/Y 戻る LIST LIMIT":u8"X/Y 戻る";
    text(cells,0,heading,false);
    text(cells,MENU_ROWS-GLYPH_VERTICAL_UNITS,footer.c_str(),false);
    (void)top;
}

void ui16_send_game_menu(const char *heading, const Ui16GameList &games, int selected, int top)
{
    std::uint16_t cells[CELL_COUNT];
#ifdef UI16_SD_FONT
    ui16_font_begin_frame(); use_sd_font=true; ui16_font_pin_existing(true);
    ui16_build_game_menu(heading, games, selected, cells, top);
    ui16_font_pin_existing(false);
#endif
    ui16_build_game_menu(heading, games, selected, cells, top);
#ifdef UI16_SD_FONT
    use_sd_font=false;
    if (!ui16_font_before_cells()) ui16_build_game_menu(heading, games, selected, cells, top);
#endif
#ifdef UI16_SD_FONT
    if(!send_cells(cells,ui16_font_raw_recovery())) {
        ui16_font_abort_frame();
        ui16_build_game_menu(heading,games,selected,cells,top);
        (void)send_cells(cells,true);
    }
    if(!ui16_font_after_cells()) {
        ui16_font_abort_frame();
        ui16_build_game_menu(heading,games,selected,cells,top);
        (void)send_cells(cells,true);
        (void)ui16_font_after_cells();
    }
#else
    (void)send_cells(cells);
#endif
}

void ui16_build_starting_game(std::uint16_t (&cells)[CELL_COUNT])
{
    for(auto &cell:cells)cell=0;
    text(cells,(MENU_ROWS-GLYPH_VERTICAL_UNITS)/2,"STARTING GAME...",false,8);
}

void ui16_send_starting_game()
{
    std::uint16_t cells[CELL_COUNT];
#ifdef UI16_SD_FONT
    ui16_font_begin_frame(); use_sd_font=true; ui16_font_pin_existing(true);
    ui16_build_starting_game(cells);
    ui16_font_pin_existing(false);
#endif
    ui16_build_starting_game(cells);
#ifdef UI16_SD_FONT
    use_sd_font=false;
    if (!ui16_font_before_cells()) ui16_build_starting_game(cells);
#endif
#ifdef UI16_SD_FONT
    if(!send_cells(cells,ui16_font_raw_recovery())) {
        ui16_font_abort_frame();
        ui16_build_starting_game(cells);
        (void)send_cells(cells,true);
    }
    if(!ui16_font_after_cells()) {
        ui16_font_abort_frame();
        ui16_build_starting_game(cells);
        (void)send_cells(cells,true);
        (void)ui16_font_after_cells();
    }
#else
    (void)send_cells(cells);
#endif
}
#endif
