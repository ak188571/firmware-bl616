#include "tc_snes_sd_guard.h"
extern "C" {
#include "sdh_sdcard.h"
status_t __wrap_SDH_ReadMultiBlocks(uint8_t *,uint32_t,uint16_t,uint32_t);
status_t __wrap_SDH_WriteMultiBlocks(uint8_t *,uint32_t,uint16_t,uint32_t);
}
#include <cassert>
#include <cstdio>
#include <string>
static status_t result=Status_Success;
static unsigned reads=0,writes=0,resets=0,masked=0;
static bool dma_active=false;
extern "C" status_t __real_SDH_ReadMultiBlocks(uint8_t *,uint32_t,uint16_t,uint32_t){reads++;dma_active=result!=0;return result;}
extern "C" status_t __real_SDH_WriteMultiBlocks(uint8_t *,uint32_t,uint16_t,uint32_t){writes++;dma_active=result!=0;return result;}
extern "C" void SDH_DisableIntSource(uint32_t mask){assert(tc_snes_sd_faulted()&&dma_active);assert(mask==(SDH_INT_DATA_COMPLETED|SDH_INT_DATA_ERRORS|SDH_INT_DMA_ERROR|SDH_INT_AUTO_CMD12_ERROR));masked++;}
extern "C" void SDH_Reset(){assert(masked==1&&tc_snes_sd_faulted());dma_active=false;resets++;}
int main(int argc,char **argv){
    assert(argc==2);const std::string scenario=argv[1];uint8_t buffer[512]{};
    assert(!tc_snes_sd_faulted());
    if(scenario=="legacy"){
        for(int error:{0,1,2,7}){
            result=error;
            assert(__wrap_SDH_ReadMultiBlocks(buffer,3,512,1)==error);
            assert(__wrap_SDH_WriteMultiBlocks(buffer,3,512,1)==error);
        }
        assert(reads==4&&writes==4&&masked==0&&resets==0&&!tc_snes_sd_faulted());
    }else if(scenario=="success"){
        {TcSnesSdScope scope(true);assert(__wrap_SDH_ReadMultiBlocks(buffer,3,512,1)==0);assert(__wrap_SDH_WriteMultiBlocks(buffer,3,512,1)==0);}
        assert(!tc_snes_sd_scope(false));assert(reads==1&&writes==1&&resets==0&&!tc_snes_sd_faulted());
    }else{
        result=scenario=="read-timeout"?Status_Timeout:7;
        {TcSnesSdScope scope(true);
            const auto status=scenario=="read-timeout"?__wrap_SDH_ReadMultiBlocks(buffer,3,512,1):__wrap_SDH_WriteMultiBlocks(buffer,3,512,1);
            // The timeout retry loop must see a hard error only after reset.
            assert(status==Status_SDH_TransferFailed&&resets==1&&masked==1&&!dma_active&&tc_snes_sd_faulted());
        }
        assert(!tc_snes_sd_scope(false));
        const auto calls=reads+writes;
        assert(__wrap_SDH_ReadMultiBlocks(buffer,3,512,1)==Status_SDH_TransferFailed);
        assert(__wrap_SDH_WriteMultiBlocks(buffer,3,512,1)==Status_SDH_TransferFailed);
        assert(reads+writes==calls&&resets==1&&!dma_active); // Latch survives scope exit.
    }
    std::puts("SNES SD guard PASS: scope, unchanged legacy returns, reset-before-buffer-reuse, no timeout retry, power-cycle fault latch");
}
