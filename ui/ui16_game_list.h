#pragma once
#include <string>
#include <vector>
#include "ui16_config.h"

inline constexpr int UI16_GAME_CAPACITY = 256;
enum class Ui16ListStatus { Ready, Empty, DirectoryError, ReadError };

struct Ui16GameList {
    std::vector<std::string> names;
    Ui16ListStatus status = Ui16ListStatus::Empty;
    bool more = false;
    std::vector<std::string> titles; // Display names only; names retains original file/folder names.
    bool titles_error = false;
    std::vector<bool> directories; // Parallel to names; classification comes from FatFs.
    bool navigation_error = false;
    const char *launch_error = nullptr; // Static error label supplied by Ui16Runtime.
};

bool ui16_is_rom_file(int core_id, const char *name);
bool ui16_valid_component(const std::string &name);
Ui16GameList ui16_read_game_list(const char *path, int core_id);
