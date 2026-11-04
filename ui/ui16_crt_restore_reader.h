#pragma once
#include "ui16_crt_restore.h"
extern "C" {
#include "ff.h"
}
// Exclusive sequential use of the existing noncacheable loader resources.
// Opens FA_READ only; never creates directories or writes records.
Ui16CrtRestore ui16_read_crt_override(const Ui16RomIdentity &id,FIL &file,void *buffer,UINT capacity);
