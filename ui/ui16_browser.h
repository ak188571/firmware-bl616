#pragma once
#include "ui16_navigation.h"
#include "ui16_game_list.h"
#include "ui16_launch.h"

inline constexpr std::uint16_t UI16_BUTTON_CONFIRM = 0x0101; // A or B
inline constexpr std::uint16_t UI16_BUTTON_BACK = 0x0202;    // X or Y
inline constexpr std::size_t UI16_MAX_FOLDER_DEPTH = 16;
inline constexpr std::size_t UI16_MAX_RELATIVE_PATH = 511;
using Ui16GameLoader = Ui16GameList (*)(int core_id, const char *relative_path, void *context);

// Retains button edges and emits launch requests; hardware remains in Ui16Runtime.
class Ui16Browser {
public:
    Ui16Browser(const Ui16MenuEntry *entries, std::size_t count, int initial = 0);
    bool update(std::uint16_t buttons, std::uint32_t now_ms,
                Ui16GameLoader loader, void *context = nullptr);
    bool in_games() const { return in_games_; }
    int selected_system() const { return systems_.selected(); }
    int selected_file() const { return files_.selected(); }
    int system_top() const { return system_top_; }
    int file_top() const { return file_top_; }
    const Ui16MenuEntry *active_system() const;
    const Ui16GameList &game_list() const { return games_; }
    const std::string &relative_path() const { return relative_path_; }
    const char *heading() const;
    bool take_launch_request(Ui16LaunchRequest &request);
    void resume_input(std::uint16_t buttons);
    void set_launch_error(const char *message) {games_.launch_error=message;}

private:
    const Ui16MenuEntry *entries_;
    std::size_t count_;
    Ui16Navigation systems_;
    Ui16Navigation files_{0};
    Ui16GameList games_;
    std::uint16_t previous_buttons_ = 0;
    bool in_games_ = false;
    Ui16LaunchRequest pending_launch_;
    int system_top_ = 0;
    int file_top_ = 0;
    struct Location { std::string component; std::string heading; int selected; int top; };
    std::vector<Location> levels_;
    std::string relative_path_;
    void load(Ui16GameLoader loader, void *context, int selected = 0);
};
