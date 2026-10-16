#include "utils.h"
#include <cassert>
#include <cstdio>
#include <vector>

struct bflb_device_s {} device;
bflb_device_s *uart1_dev = &device;
int critical_depth = 0;
static std::vector<unsigned char> bytes;
void enter_critical() { assert(critical_depth == 0); ++critical_depth; }
void leave_critical() { assert(critical_depth == 1); --critical_depth; }
void bflb_uart_putchar(bflb_device_s *dev, int value) {
    assert(dev == &device && critical_depth == 1);
    bytes.push_back(static_cast<unsigned char>(value));
}
void packet(int command) {
    taskENTER_CRITICAL();
    fpga_tx_header(command,3);
    fpga_tx_byte('A'); fpga_tx_byte('B');
    taskEXIT_CRITICAL();
}
int main() {
    packet(5);
    assert((bytes == std::vector<unsigned char>{0xaa,0,3,5,'A','B'}));
    bytes.clear();
#ifdef UI16_SYSTEM_MENU
    fpga_use_ui16_display(true);
    packet(5);
    assert((bytes == std::vector<unsigned char>{0xaa,0,3,13,'A','B'}));
    bytes.clear(); packet(14);
    assert((bytes == std::vector<unsigned char>{0xaa,0,3,14,'A','B'}));
    bytes.clear();
    fpga_use_ui16_display(false);
#endif
    packet(5);
    assert((bytes == std::vector<unsigned char>{0xaa,0,3,5,'A','B'}));
    assert(critical_depth == 0);
    std::puts("UART ownership test passed: legacy / debug-only UI16 / restored legacy; payload unchanged");
}
