#pragma once
#include <cstdint>
inline constexpr std::uint16_t UI16_GLYPH_MASK = 0x0fff;
inline constexpr std::uint16_t UI16_SELECTED = 0x1000;
inline constexpr std::uint16_t UI16_HALF = 0x2000;
inline constexpr std::uint8_t ui16_wire_high(std::uint16_t cell) {
    return static_cast<std::uint8_t>(((cell >> 6) & 0x3c) | ((cell >> 12) & 3));
}
