#pragma once
#include <cstdint>
// Font updates are always gated, not safe while a pixel is visible.
// Call blank, send each bitmap, send all 28 UI16 rows, then show.
// Reliable builds wait for CRC32-protected ACKs; the legacy sender remains
// available for safe fixed-font recovery when a negotiated link fails.
bool ui16_font_blank();
bool ui16_font_write(std::uint16_t slot, const std::uint8_t (&rows)[32]);
bool ui16_font_show();
void ui16_font_raw_show();
void ui16_font_raw_blank();
