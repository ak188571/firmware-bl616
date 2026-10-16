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
