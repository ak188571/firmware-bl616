#include "tc_snes_sd_guard.h"
#if defined(UI16_SYSTEM_MENU) && defined(BL616)
extern "C" {
#include "sdh_sdcard.h"
status_t __real_SDH_ReadMultiBlocks(uint8_t *,uint32_t,uint16_t,uint32_t);
status_t __real_SDH_WriteMultiBlocks(uint8_t *,uint32_t,uint16_t,uint32_t);
}
namespace {
bool snes_scope=false;
bool faulted=false;
status_t finish(status_t status){
    if(snes_scope&&status!=Status_Success){
        // The SDK's timeout path does not abort ADMA. Complete a controller
        // reset before allowing any caller to reuse its DMA buffer. Do not
        // attempt another SD operation until power is cycled, since the SDK
        // caches its initialized-card state. USB storage is outside this scope.
        faulted=true;
        SDH_DisableIntSource(SDH_INT_DATA_COMPLETED|SDH_INT_DATA_ERRORS|SDH_INT_DMA_ERROR|SDH_INT_AUTO_CMD12_ERROR);
        SDH_Reset(); // SDK waits for SW_RST_ALL to clear before returning.
        return Status_SDH_TransferFailed; // Never feed the port's timeout retry loop.
    }
    return status;
}
}
bool tc_snes_sd_scope(bool active){const bool previous=snes_scope;snes_scope=active;return previous;}
bool tc_snes_sd_faulted(){return faulted;}
extern "C" status_t __wrap_SDH_ReadMultiBlocks(uint8_t *data,uint32_t address,uint16_t size,uint32_t count){
    if(faulted)return Status_SDH_TransferFailed;
    return finish(__real_SDH_ReadMultiBlocks(data,address,size,count));
}
extern "C" status_t __wrap_SDH_WriteMultiBlocks(uint8_t *data,uint32_t address,uint16_t size,uint32_t count){
    if(faulted)return Status_SDH_TransferFailed;
    return finish(__real_SDH_WriteMultiBlocks(data,address,size,count));
}
#else
bool tc_snes_sd_scope(bool){return false;}
bool tc_snes_sd_faulted(){return false;}
#endif
