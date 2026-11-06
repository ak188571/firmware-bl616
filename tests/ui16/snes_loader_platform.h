#pragma once
#include <cstdint>
extern "C" {
#include "ff.h"
}
extern bool core_running;
extern FIL fcore;
extern unsigned char fbuf[8192];
void send_fbuf_data(std::uint16_t);
void set_loading_state(int);
void delay(unsigned);
char *strcasestr(const char *,const char *); // MinGW lacks this GNU libc entry point.
#define DEBUG(...) ((void)0)
