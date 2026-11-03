#pragma once
#include "ui16_rom_id.h"
extern "C" {
#include "ff.h"
}

// Caller supplies the existing noncacheable loader file/buffer, while neither
// loader nor any other storage operation owns them. Opens read-only and closes
// before returning. An unsuccessful identity never prevents normal ROM loading.
Ui16RomIdentity ui16_identify_rom(int system,const char *path,FIL &file,
                                void *buffer,UINT capacity,void (*yield)()=nullptr);
