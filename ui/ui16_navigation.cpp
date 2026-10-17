#ifdef UI16_SYSTEM_MENU
#include "ui16_navigation.h"
#include <limits>

Ui16Navigation::Ui16Navigation(std::size_t count, int initial)
    : count_(static_cast<int>(count > static_cast<std::size_t>(std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max() : count)),
      selected_(count_ == 0 ? -1 : initial < 0 ? 0 : initial >= count_ ? count_ - 1 : initial)
{
}

bool Ui16Navigation::move(int direction)
{
    if (count_ == 0) return false;
    const int next = selected_ + direction;
    if (next < 0 || next >= count_) return false;
    selected_ = next;
    return true;
}

bool Ui16Navigation::update(std::uint16_t buttons, std::uint32_t now_ms)
{
    const bool up = (buttons & UI16_BUTTON_UP) != 0;
    const bool down = (buttons & UI16_BUTTON_DOWN) != 0;
    const int direction = up == down ? 0 : up ? -1 : 1;
    if (direction == 0) {
        held_ = 0;
        repeating_ = false;
        return false;
    }
    if (direction != held_) {
        held_ = direction;
        pressed_ms_ = now_ms;
        repeating_ = false;
        return move(direction);
    }
    // Unsigned subtraction keeps these intervals valid across the 32-bit timer wrap.
    if (!repeating_ && now_ms - pressed_ms_ >= UI16_REPEAT_DELAY_MS) {
        repeating_ = true;
        repeated_ms_ = now_ms;
        return move(direction);
    }
    if (repeating_ && now_ms - repeated_ms_ >= UI16_REPEAT_INTERVAL_MS) {
        repeated_ms_ = now_ms;
        return move(direction);
    }
    return false;
}
#endif
