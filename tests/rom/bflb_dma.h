#pragma once
#include "rom_uart_platform.h"
struct bflb_dma_channel_lli_pool_s {unsigned unused;};
struct bflb_dma_channel_config_s {int direction,src_req,dst_req,src_addr_inc,dst_addr_inc,src_burst_count,dst_burst_count,src_width,dst_width;};
struct bflb_dma_channel_lli_transfer_s {std::uintptr_t src_addr,dst_addr;std::size_t nbytes;};
enum {DMA_MEMORY_TO_PERIPH,DMA_REQUEST_NONE,DMA_REQUEST_UART1_TX,DMA_ADDR_INCREMENT_ENABLE,DMA_ADDR_INCREMENT_DISABLE,DMA_BURST_INCR1,DMA_DATA_WIDTH_8BIT,DMA_ADDR_UART1_TDR};
bool bflb_dma_channel_isbusy(bflb_device_s *);
void bflb_dma_channel_init(bflb_device_s *,bflb_dma_channel_config_s *);
void bflb_dma_channel_tcint_mask(bflb_device_s *,bool);
void bflb_dma_channel_tcint_clear(bflb_device_s *);
int bflb_dma_channel_lli_reload(bflb_device_s *,bflb_dma_channel_lli_pool_s *,int,bflb_dma_channel_lli_transfer_s *,int);
void bflb_dma_channel_start(bflb_device_s *);
void bflb_dma_channel_stop(bflb_device_s *);
