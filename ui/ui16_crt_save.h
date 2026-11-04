#pragma once
#include "ui16_crt_restore_reader.h"
// The caller owns the supplied noncacheable file/buffer for this whole operation.
// Writes a user override only. Reset/tombstone creation is a separate stage.
bool ui16_save_crt_override(const Ui16RomIdentity &id,const Ui16CrtSettings &value,
                           FIL &file,void *buffer,UINT capacity);
