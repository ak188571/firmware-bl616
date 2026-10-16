#pragma once

// 0x05 writes the legacy character RAM and switches the FPGA out of UI16 mode.
// 0x0d carries the same text as debug-only data, preserving the displayed UI16 cells.
inline constexpr int ui16_display_command(int command, bool ui16_active)
{
    return ui16_active && command == 0x05 ? 0x0d : command;
}
