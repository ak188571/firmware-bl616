#pragma once
#include <cstdint>
// Host-test shim for transmission and FreeRTOS critical sections. Not a firmware header.
extern int critical_depth;
extern void enter_critical();
extern void leave_critical();
#define taskENTER_CRITICAL() enter_critical()
#define taskEXIT_CRITICAL() leave_critical()
void fpga_tx_header(int command,int length);
void fpga_tx_byte(std::uint8_t byte);

// Host declarations for the real UART send functions extracted from utils.cpp.
struct bflb_device_s;
extern bflb_device_s *uart1_dev;
void bflb_uart_putchar(bflb_device_s *dev, int value);
void fpga_use_ui16_display(bool active);

// Declarations for compiling the real in-game popup input loop on the host.
void get_joypad_states(std::uint16_t *,std::uint16_t *,std::uint16_t *,std::uint16_t *);
void delay(unsigned);
#define OSD_KEY_CODE 0x84
#define DEBUG(...) ((void)0)

std::uint64_t bflb_mtimer_get_time_ms();
