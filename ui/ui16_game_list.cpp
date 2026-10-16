#ifdef UI16_SYSTEM_MENU
#include "ui16_game_list.h"
#include <cstring>
extern "C" {
#include "ff.h"
}

namespace {
bool equal_extension(const char *value, const char *expected)
{
    while (*value && *expected) {
        unsigned char c = static_cast<unsigned char>(*value++);
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != static_cast<unsigned char>(*expected++)) return false;
    }
    return *value == 0 && *expected == 0;
}
}

bool ui16_is_rom_file(int core_id, const char *name)
{
    if (!name || !*name) return false;
    const char *ext = std::strrchr(name, '.');
    if (!ext || ext == name) return false;
    switch (core_id) {
    case 1: return equal_extension(ext, ".nes");
    case 2: return equal_extension(ext, ".smc") || equal_extension(ext, ".sfc");
    case 3: return equal_extension(ext, ".gba");
    case 4: return equal_extension(ext, ".bin") || equal_extension(ext, ".md");
    case 5: return equal_extension(ext, ".sms");
    case 6: return equal_extension(ext, ".img");
    default: return false;
    }
}

Ui16GameList ui16_read_game_list(const char *path, int core_id)
{
    Ui16GameList result;
    DIR directory;
    if (!path || f_opendir(&directory, path) != FR_OK) {
        result.status = Ui16ListStatus::DirectoryError;
        return result;
    }
    FILINFO info{};
    for (;;) {
        if (f_readdir(&directory, &info) != FR_OK) {
            result.status = Ui16ListStatus::ReadError;
            break;
        }
        if (!info.fname[0]) break;
        if ((info.fattrib & (AM_DIR | AM_HID | AM_SYS)) || !ui16_is_rom_file(core_id, info.fname)) continue;
        if (result.names.size() == UI16_GAME_CAPACITY) {
            result.more = true;
            break;
        }
        result.names.emplace_back(info.fname);
    }
    if (f_closedir(&directory) != FR_OK) result.status = Ui16ListStatus::ReadError;
    if (result.status == Ui16ListStatus::ReadError) {
        result.names.clear();
        result.more = false;
    } else {
        result.status = result.names.empty() ? Ui16ListStatus::Empty : Ui16ListStatus::Ready;
    }
    return result;
}
#endif
