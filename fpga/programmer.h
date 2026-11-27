#ifndef PROGRAMMER_H
#define PROGRAMMER_H

#include <cstddef>
#include "monitor_boot_wait.h"

#ifdef __cplusplus
extern "C" {
#endif

// main function: program FPGA with a bitstream (.bin) file
// return true if successful
extern bool fpga_program(const char *fname, MonitorBootWait wait);

#ifdef __cplusplus
}
#endif

#endif // PROGRAMMER_H
