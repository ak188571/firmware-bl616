#pragma once
#ifdef TC_LAUNCH_TRACE
extern "C" {
#include "ff.h"
}
// Call only after returning to the monitor, under the application's storage lease.
// The supplied FIL and buffer must be DMA-safe (the existing fcore/fbuf).
// Creates a new file; never replaces a previous measurement or user file.
bool tc_launch_export(const char *rom,int core_id,FIL &file,void *buffer,UINT capacity,
                      char *output_path,UINT output_capacity);
#endif
