#pragma once
#include <cstddef>
#include <cstdint>
#include "ui16_config.h"

struct Ui16MenuEntry {
    int id;
    const char *name;
};

inline constexpr int UI16_MENU_FIRST_ROW = 3;
inline constexpr int UI16_MENU_CAPACITY = MENU_ROWS - UI16_MENU_FIRST_ROW - 1;

// Pure layout function, also used by host tests. Unknown glyphs remain blank.
void ui16_build_system_menu(const Ui16MenuEntry *entries, std::size_t count,
                           std::uint16_t (&cells)[CELL_COUNT]);
void ui16_send_system_menu(const Ui16MenuEntry *entries, std::size_t count);
