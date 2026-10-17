#pragma once
#include <cstddef>
#include <cstdint>
#include "ui16_config.h"
#include "ui16_game_list.h"
#include "ui16_text_layout.h"

struct Ui16MenuEntry {
    int id;
    const char *name;
};

const char *ui16_system_label(const Ui16MenuEntry &entry);

inline constexpr int UI16_MENU_FIRST_ROW = 3;
inline constexpr int UI16_MENU_CAPACITY = MENU_ROWS - UI16_MENU_FIRST_ROW - 1;

// Keep selection visible without paging; top is an item index.
int ui16_scroll_top(int selected, std::size_t count, int top);
int ui16_system_scroll_top(const Ui16MenuEntry *entries, std::size_t count, int selected, int top);
int ui16_game_scroll_top(const Ui16GameList &games, int selected, int top);
std::string ui16_game_label(const Ui16GameList &games, std::size_t index);

// Pure layout function. -1 preserves the stage-1 default (first positive core ID).
// Other invalid selection indices select no row. Unknown glyphs remain blank.
void ui16_build_system_menu(const Ui16MenuEntry *entries, std::size_t count,
                           std::uint16_t (&cells)[CELL_COUNT], int selected_index = -1, int top = -1);
void ui16_send_system_menu(const Ui16MenuEntry *entries, std::size_t count,
                           int selected_index = -1, int top = -1);
void ui16_build_game_menu(const char *heading, const Ui16GameList &games, int selected,
                         std::uint16_t (&cells)[CELL_COUNT], int top = -1);
void ui16_send_game_menu(const char *heading, const Ui16GameList &games, int selected, int top = -1);
