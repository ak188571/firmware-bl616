#pragma once
#include <cstdint>
#ifdef TC_ROM_UART
extern "C" {
#include "ff.h"
}
bool tc_rom_uart_owned();
void tc_rom_uart_capabilities(bool protocol,bool uart8);
bool tc_rom_uart_capable();
bool tc_rom_uart_load_snes(FIL &file,std::uint32_t off,std::uint32_t bytes,const std::uint8_t *header);
// The application owns the single RX ISR ring. Its ordinary parser pauses
// while the ROM adapter is the consumer; interrupts remain enabled.
int tc_rom_uart_rx_take(std::uint8_t *byte);
void tc_rom_uart_rx_flush();
#else
inline bool tc_rom_uart_owned(){return false;}
inline void tc_rom_uart_capabilities(bool,bool){}
inline bool tc_rom_uart_capable(){return false;}
#endif
