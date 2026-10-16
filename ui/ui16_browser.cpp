#ifdef UI16_SYSTEM_MENU
#include "ui16_browser.h"

Ui16Browser::Ui16Browser(const Ui16MenuEntry *entries, std::size_t count, int initial)
    : entries_(entries), count_(entries ? count : 0), systems_(count_, initial)
{
}

const Ui16MenuEntry *Ui16Browser::active_system() const
{
    const int selected = systems_.selected();
    if (selected < 0 || static_cast<std::size_t>(selected) >= count_) return nullptr;
    return entries_ + selected;
}

bool Ui16Browser::update(std::uint16_t buttons, std::uint32_t now_ms,
                         Ui16GameLoader loader, void *context)
{
    const auto pressed = static_cast<std::uint16_t>(buttons & ~previous_buttons_);
    previous_buttons_ = buttons;
    const bool both = (buttons & UI16_BUTTON_CONFIRM) && (buttons & UI16_BUTTON_BACK);
    if (!both && in_games_ && (pressed & UI16_BUTTON_BACK)) {
        in_games_ = false;
        systems_.update(0, now_ms); // Reset repeat timing, preserving the system selection.
        return true;
    }
    if (!both && !in_games_ && (pressed & UI16_BUTTON_CONFIRM)) {
        const auto *entry = active_system();
        if (entry && entry->id > 0) {
            games_ = loader ? loader(entry->id, context) : Ui16GameList{};
            if (!loader) games_.status = Ui16ListStatus::DirectoryError;
            files_ = Ui16Navigation(games_.names.size());
            in_games_ = true;
            return true;
        }
    }
    return in_games_ ? files_.update(buttons, now_ms) : systems_.update(buttons, now_ms);
}
#endif
