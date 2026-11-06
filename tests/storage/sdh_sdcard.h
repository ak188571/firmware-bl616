#pragma once
#include <stdint.h>
typedef int status_t;
enum {Status_Success=0,Status_Timeout=1,Status_SDH_TransferFailed=2};
#define SDH_INT_DATA_COMPLETED 0x2u
#define SDH_INT_DATA_ERRORS 0x01700000u
#define SDH_INT_DMA_ERROR 0x2000000u
#define SDH_INT_AUTO_CMD12_ERROR 0x1000000u
void SDH_DisableIntSource(uint32_t);
void SDH_Reset(void);
