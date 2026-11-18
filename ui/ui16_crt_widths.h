#pragma once
#include "utils.h"
#include <array>

// NES and SNES latch the four big-endian bytes of command 0x14 together.
inline void ui16_send_crt_widths(const std::array<std::uint8_t,4> &widths){
    taskENTER_CRITICAL();
    fpga_tx_header(0x14,5);
    for(auto width:widths)fpga_tx_byte(width);
    taskEXIT_CRITICAL();
}
