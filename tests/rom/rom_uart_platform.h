#pragma once
#include <cstdint>
extern "C" {
#include "ff.h"
}
#define ATTR_NOCACHE_NOINIT_RAM_SECTION
struct bflb_device_s {std::uintptr_t reg_base;};
extern bflb_device_s *uart1_dev;
extern "C" {
std::uint32_t bflb_mtimer_get_time_ms();
std::uint64_t bflb_mtimer_get_time_us();
bool bflb_uart_txempty(bflb_device_s *);
void bflb_uart_link_txdma(bflb_device_s *,bool);
int bflb_uart_feature_control(bflb_device_s *,int,std::uint32_t);
bflb_device_s *bflb_device_get_by_name(const char *);
std::uint32_t getreg32(std::uintptr_t);
void mock_enter();void mock_leave();
}
void delay(unsigned);
#define taskENTER_CRITICAL() mock_enter()
#define taskEXIT_CRITICAL() mock_leave()
enum {UART_CMD_SET_BAUD_RATE=1,UART_CMD_CLR_RX_FIFO=2,UART_CMD_CLR_TX_FIFO=3};
