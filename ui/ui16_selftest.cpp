#ifdef UI16_UART_SELFTEST
#include "utils.h"
#include "ui16_config.h"
#include "ui16_selftest_cells.h"

// Test-only sender. Main task parks after sending so legacy menu redraws cannot replace it.
void ui16_send_selftest()
{
    for (int y=0;y<MENU_ROWS;++y) {
        taskENTER_CRITICAL();
        fpga_tx_header(ROW_COMMAND,4+2*TEXT_UNITS_PER_ROW);
        fpga_tx_byte(static_cast<uint8_t>(y));
        fpga_tx_byte(0);
        fpga_tx_byte(TEXT_UNITS_PER_ROW);
        for (int x=0;x<TEXT_UNITS_PER_ROW;++x) {
            auto cell=ui16_selftest_cells[y*TEXT_UNITS_PER_ROW+x];
            fpga_tx_byte(static_cast<uint8_t>(cell&255));
            fpga_tx_byte(static_cast<uint8_t>((cell>>8)&ATTR_MASK));
        }
        taskEXIT_CRITICAL();
    }
}
#endif
