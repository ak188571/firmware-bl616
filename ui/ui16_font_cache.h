#pragma once
#include <cstdint>
#ifdef UI16_SD_FONT
// These hooks are used only by the UI16 title/system menu, never by game OSD.
void ui16_font_open(const char *path);
void ui16_font_monitor_reset();
void ui16_font_begin_frame();
// First rendering pass pins resident glyphs before any slot is recycled.
void ui16_font_pin_existing(bool enabled);
std::uint16_t ui16_font_resolve(std::uint32_t cp, std::uint16_t fallback);
// Uploads glyphs into unused or hidden slots while the display stays on.
// BLANK is sent only on the initial frame or when a visible slot must change.
// On false, rebuild current cells with the unchanged built-in font map.
bool ui16_font_before_cells();
// Sends SHOW if BLANK was used and commits the current visible slot set.
bool ui16_font_after_cells();
// True when this frame uses CRC-protected row commands.
bool ui16_font_frame_gated();
bool ui16_font_raw_recovery();
void ui16_font_abort_frame();
void ui16_font_start_raw_recovery();
#endif
