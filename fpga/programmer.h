#ifndef PROGRAMMER_H
#define PROGRAMMER_H

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// main function: program FPGA with a bitstream (.bin) file
// return true if successful
extern bool fpga_program(const char *fname);
// Best-effort, bounded append to the mounted volume; never call in a critical section.
extern void fpga_boot_trace(const char *phase, const char *detail);

#ifdef __cplusplus
}
#endif

#endif // PROGRAMMER_H
