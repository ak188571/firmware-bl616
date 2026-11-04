#pragma once
#include "ui16_crt_defaults.h"
extern "C" {
#include "ff.h"
}
// Sequential exclusive use of existing noncacheable loader file/buffer.
// Reads <=4096 bytes on the ROM's volume only. No file creation or writing.
Ui16CrtDefaults ui16_read_crt_defaults(int system,const char *rom_path,FIL &file,
                                    void *buffer,UINT capacity);
