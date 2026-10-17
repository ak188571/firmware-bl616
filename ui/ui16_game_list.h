#pragma once
#include <string>
#include <vector>
#include "ui16_config.h"

inline constexpr int UI16_GAME_CAPACITY = MENU_ROWS - 4;
enum class Ui16ListStatus { Ready, Empty, DirectoryError, ReadError };

struct Ui16GameList {
    std::vector<std::string> names;
    Ui16ListStatus status = Ui16ListStatus::Empty;
    bool more = false;
    std::vector<std::string> titles; // Display names only; names always retains ROM filenames.
    bool titles_error = false;
};

bool ui16_is_rom_file(int core_id, const char *name);
Ui16GameList ui16_read_game_list(const char *path, int core_id);
