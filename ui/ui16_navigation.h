#pragma once
#include <cstddef>
#include <cstdint>
#include "ui16_system_menu.h"

inline constexpr std::uint16_t UI16_BUTTON_UP = 0x10;
inline constexpr std::uint16_t UI16_BUTTON_DOWN = 0x20;
inline constexpr std::uint32_t UI16_REPEAT_DELAY_MS = 400;
inline constexpr std::uint32_t UI16_REPEAT_INTERVAL_MS = 100;

// Pure input state machine. One call moves at most one item, even after a long stall.
class Ui16Navigation {
public:
    explicit Ui16Navigation(std::size_t count, int initial = 0);
    int selected() const { return selected_; } // -1 for an empty menu
    bool update(std::uint16_t buttons, std::uint32_t now_ms);

private:
    bool move(int direction);
    int count_;
    int selected_;
    int held_ = 0;
    bool repeating_ = false;
    std::uint32_t pressed_ms_ = 0;
    std::uint32_t repeated_ms_ = 0;
};
