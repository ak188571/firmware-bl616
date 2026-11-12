#ifdef TC_JTAG_SPI_HZ
#include "tc_jtag_spi.h"
#include <initializer_list>
extern "C" {
#include "bflb_spi.h"
#include "bflb_dma.h"
#include "bflb_gpio.h"
#include "bflb_clock.h"
#include "bflb_l1c.h"
#include "hardware/spi_reg.h"
#include "hardware/dma_reg.h"
#include "hardware/gpio_reg.h"
}

namespace {
constexpr std::uint32_t DmaBase=0x2000c000u, Role=0x20000510u;
constexpr std::uint32_t Channels=(1u<<1)|(1u<<2);
constexpr unsigned SpiOffsets[]={SPI_CONFIG_OFFSET,SPI_PRD_0_OFFSET,SPI_PRD_1_OFFSET,
    SPI_RXD_IGNR_OFFSET,SPI_FIFO_CONFIG_0_OFFSET,SPI_FIFO_CONFIG_1_OFFSET,SPI_INT_STS_OFFSET};
constexpr unsigned DmaOffsets[]={DMA_CxSRCADDR_OFFSET,DMA_CxDSTADDR_OFFSET,DMA_CxLLI_OFFSET,
    DMA_CxCONTROL_OFFSET,DMA_CxCONFIG_OFFSET};
constexpr std::uint32_t GpioBaseConfig=GLB_REG_GPIO_0_INT_MASK|GLB_REG_GPIO_0_OE|
    GLB_REG_GPIO_0_IE|GLB_REG_GPIO_0_SMT|(3u<<GLB_REG_GPIO_0_DRV_SHIFT)|
    (11u<<GLB_REG_GPIO_0_FUNC_SEL_SHIFT)|(1u<<GLB_REG_GPIO_0_MODE_SHIFT);
constexpr std::uint32_t NativeMask=GLB_REG_GPIO_0_FUNC_SEL_MASK|GLB_REG_GPIO_0_MODE_MASK|
    GLB_REG_GPIO_0_OE|GLB_REG_GPIO_0_IE|GLB_REG_GPIO_0_I;
constexpr std::uint32_t NativeValue=(11u<<GLB_REG_GPIO_0_FUNC_SEL_SHIFT)|
    (1u<<GLB_REG_GPIO_0_MODE_SHIFT)|GLB_REG_GPIO_0_OE|GLB_REG_GPIO_0_IE|GLB_REG_GPIO_0_I;
bflb_device_s *spi, *gpio, *tx, *rx;
std::uint32_t saved_spi[7],saved_dma[2][5],saved_role,saved_enable;
bool owned=false,muxed=false,finished=false,healthy=false;
bool last_tdi=true;
TcJtagSpiStats stats;
// 8192 bytes need three SDK LLI entries (4064 + 4064 + 64).
static ATTR_NOCACHE_NOINIT_RAM_SECTION bflb_dma_channel_lli_pool_s tx_lli[3] __attribute__((aligned(64)));
static ATTR_NOCACHE_NOINIT_RAM_SECTION bflb_dma_channel_lli_pool_s rx_lli[3] __attribute__((aligned(64)));
static ATTR_NOCACHE_NOINIT_RAM_SECTION std::uint8_t discard[64] __attribute__((aligned(64)));
std::uint32_t sr(unsigned off){return getreg32(spi->reg_base+off);}
void sw(unsigned off,std::uint32_t value){putreg32(value,spi->reg_base+off);}
std::uint32_t pin(unsigned n){return getreg32(gpio->reg_base+GLB_GPIO_CFG0_OFFSET+4*n);}
void drive(unsigned n,bool high){
    putreg32(GpioBaseConfig|(high?GLB_REG_GPIO_0_SET:GLB_REG_GPIO_0_CLR),
             gpio->reg_base+GLB_GPIO_CFG0_OFFSET+4*n);
}
bool native(){return (pin(28)&NativeMask)==NativeValue;}
bool fail(TcJtagSpiError error){
    if(stats.error==TcJtagSpiError::None)stats.error=error;
    healthy=false;return false;
}
bool dma_error(){return getreg32(DmaBase+DMA_RAWINTERRORSTATUS_OFFSET)&Channels;}
bool fifo_error(){return sr(SPI_FIFO_CONFIG_0_OFFSET)&0xf0u;}
bool fifo_empty(){
    const auto value=sr(SPI_FIFO_CONFIG_1_OFFSET);
    return !(value&SPI_RX_FIFO_CNT_MASK) &&
        ((value&SPI_TX_FIFO_CNT_MASK)>>SPI_TX_FIFO_CNT_SHIFT)==SPI_FIFO_BYTE_NUM_MAX;
}
void clear_status(){
    putreg32(Channels,DmaBase+DMA_INTTCCLEAR_OFFSET);
    putreg32(Channels,DmaBase+DMA_INTERRCLR_OFFSET);
}
bool stop_dma(){
    bflb_spi_link_txdma(spi,false);bflb_spi_link_rxdma(spi,false);
    bflb_dma_channel_stop(tx);bflb_dma_channel_stop(rx);
    const auto deadline=bflb_mtimer_get_time_us()+1000;
    while((getreg32(tx->reg_base+DMA_CxCONFIG_OFFSET)|getreg32(rx->reg_base+DMA_CxCONFIG_OFFSET))&(DMA_E|DMA_A)){
        if(bflb_mtimer_get_time_us()>=deadline)return fail(TcJtagSpiError::StopTimeout);
    }
    return true;
}
void park_pins(){
    if(!muxed)return;
    // On success SPI is idle-high and empty. Reclaim TCK before disabling SPI.
    // On error a partial bitstream is abandoned; no further data is sent.
    drive(1,true);bflb_mtimer_delay_us(2);
    drive(3,last_tdi);
    muxed=false;
}
bool attach(){
    if(!native())return fail(TcJtagSpiError::NativePin);
    // writeSRAM_start() must have left GPIO TCK high and TMS low in SHIFT_DR.
    drive(0,false);drive(1,true);
    bflb_mtimer_delay_us(2);
    // SET/CLEAR mode's output-value bit is not the pad level. Sense through IE.
    if((pin(0)&GLB_REG_GPIO_0_I) || !(pin(1)&GLB_REG_GPIO_0_I))
        return fail(TcJtagSpiError::State);
    // No hardware CS pin: GPIO0 remains software TMS. GPIO2 remains JTAG TDO;
    // RX DMA discards the SPI input, which is not used to validate JTAG.
    bflb_gpio_init(gpio,3,GPIO_FUNC_SPI0|GPIO_ALTERNATE|GPIO_FLOAT|GPIO_SMT_EN|GPIO_DRV_3);
    bflb_gpio_init(gpio,1,GPIO_FUNC_SPI0|GPIO_ALTERNATE|GPIO_FLOAT|GPIO_SMT_EN|GPIO_DRV_3);
    muxed=true;bflb_mtimer_delay_us(2);
    return true;
}
bool send_dma(const std::uint8_t *data,std::size_t size){
    bflb_dma_channel_lli_transfer_s t{},r{};
    t.src_addr=reinterpret_cast<std::uintptr_t>(data);t.dst_addr=DMA_ADDR_SPI0_TDR;t.nbytes=size;
    r.src_addr=DMA_ADDR_SPI0_RDR;r.dst_addr=reinterpret_cast<std::uintptr_t>(discard);r.nbytes=size;
    clear_status();
    // fbuf is noncacheable today. Clean explicitly so other callers cannot
    // accidentally transmit stale cached data. RX uses dedicated noncache RAM.
    bflb_l1c_dcache_clean_range(reinterpret_cast<void*>(const_cast<std::uint8_t*>(data)),size);
    const int nt=bflb_dma_channel_lli_reload(tx,tx_lli,3,&t,1);
    const int nr=bflb_dma_channel_lli_reload(rx,rx_lli,3,&r,1);
    if(nt<1||nt>3||nr!=nt)return fail(TcJtagSpiError::Descriptor);
    bflb_spi_link_rxdma(spi,true);bflb_spi_link_txdma(spi,true);
    bflb_dma_channel_start(rx);bflb_dma_channel_start(tx);
    // Retain the bounded 100 ms allowance used by the 10 MHz trial.
    const auto deadline=bflb_mtimer_get_time_us()+100000;
    for(;;){
        if(dma_error())return fail(TcJtagSpiError::Dma);
        if(fifo_error())return fail(TcJtagSpiError::Fifo);
        const bool idle=!bflb_dma_channel_isbusy(tx)&&!bflb_dma_channel_isbusy(rx)&&
            !(sr(SPI_BUS_BUSY_OFFSET)&SPI_STS_SPI_BUS_BUSY);
        if(idle)break;
        if(bflb_mtimer_get_time_us()>=deadline)return fail(TcJtagSpiError::Timeout);
    }
    if(!stop_dma())return false;
    if(dma_error())return fail(TcJtagSpiError::Dma);
    if(fifo_error())return fail(TcJtagSpiError::Fifo);
    if(!fifo_empty() || (getreg32(DmaBase+DMA_RAWINTTCSTATUS_OFFSET)&Channels)!=Channels)
        return fail(TcJtagSpiError::Incomplete);
    last_tdi=data[size-1]&1;
    stats.dma_bytes+=size;++stats.dma_blocks;
    return true;
}
}

const TcJtagSpiStats &tc_jtag_spi_stats(){return stats;}

bool tc_jtag_spi_prepare(){
    if(owned)return false; // A previous non-quiescent DMA requires recovery/reset.
    stats={};healthy=false;finished=false;last_tdi=true;
    spi=bflb_device_get_by_name("spi0");gpio=bflb_device_get_by_name("gpio");
    tx=bflb_device_get_by_name("dma0_ch1");rx=bflb_device_get_by_name("dma0_ch2");
    if(!spi||!gpio||!tx||!rx||tx->idx!=0||rx->idx!=0||tx->sub_idx!=1||rx->sub_idx!=2)
        return fail(TcJtagSpiError::Resources);
    if(!native())return fail(TcJtagSpiError::NativePin);
    for(unsigned i=0;i<7;++i)saved_spi[i]=sr(SpiOffsets[i]);
    if((saved_spi[0]&(SPI_CR_SPI_M_EN|SPI_CR_SPI_S_EN)) ||
       (sr(SPI_BUS_BUSY_OFFSET)&SPI_STS_SPI_BUS_BUSY) || !fifo_empty() ||
       (saved_spi[4]&(SPI_DMA_TX_EN|SPI_DMA_RX_EN)) || fifo_error() ||
       ((getreg32(tx->reg_base+DMA_CxCONFIG_OFFSET)|getreg32(rx->reg_base+DMA_CxCONFIG_OFFSET))&(DMA_E|DMA_A)) ||
       (getreg32(DmaBase+DMA_RAWINTERRORSTATUS_OFFSET)&Channels))
        return fail(TcJtagSpiError::Resources);
    for(unsigned i=0;i<5;++i){saved_dma[0][i]=getreg32(tx->reg_base+DmaOffsets[i]);saved_dma[1][i]=getreg32(rx->reg_base+DmaOffsets[i]);}
    saved_role=getreg32(Role)&(1u<<12);saved_enable=getreg32(DmaBase+DMA_TOP_CONFIG_OFFSET)&DMA_E;
    owned=true;
    bflb_dma_channel_config_s cfg{};
    cfg.direction=DMA_MEMORY_TO_PERIPH;cfg.src_req=DMA_REQUEST_NONE;cfg.dst_req=DMA_REQUEST_SPI0_TX;
    cfg.src_addr_inc=DMA_ADDR_INCREMENT_ENABLE;cfg.dst_addr_inc=DMA_ADDR_INCREMENT_DISABLE;
    cfg.src_burst_count=cfg.dst_burst_count=DMA_BURST_INCR1;
    cfg.src_width=cfg.dst_width=DMA_DATA_WIDTH_8BIT;
    bflb_dma_channel_init(tx,&cfg);bflb_dma_channel_tcint_mask(tx,true);
    cfg.direction=DMA_PERIPH_TO_MEMORY;cfg.src_req=DMA_REQUEST_SPI0_RX;cfg.dst_req=DMA_REQUEST_NONE;
    cfg.src_addr_inc=DMA_ADDR_INCREMENT_DISABLE;
    bflb_dma_channel_init(rx,&cfg);bflb_dma_channel_tcint_mask(rx,true);
    // BL616 IE/ITC are mask bits. Poll raw status instead of invoking shared ISR.
    for(auto *d:{tx,rx})putreg32(getreg32(d->reg_base+DMA_CxCONFIG_OFFSET)|DMA_IE|DMA_ITC,d->reg_base+DMA_CxCONFIG_OFFSET);
    sw(SPI_INT_STS_OFFSET,0x3f00u);
    const bflb_spi_config_s config={tc_jtag_spi_hz,SPI_ROLE_MASTER,SPI_MODE3,
        SPI_DATA_WIDTH_8BIT,SPI_BIT_MSB,SPI_BYTE_LSB,0,0};
    bflb_spi_init(spi,&config);
    const bool continuous=bflb_spi_feature_control(spi,SPI_CMD_SET_CS_INTERVAL,true)==0;
    stats.config=sr(SPI_CONFIG_OFFSET);stats.period=sr(SPI_PRD_0_OFFSET);
    const auto p0=(stats.period&SPI_CR_SPI_PRD_D_PH_0_MASK)>>SPI_CR_SPI_PRD_D_PH_0_SHIFT;
    const auto p1=(stats.period&SPI_CR_SPI_PRD_D_PH_1_MASK)>>SPI_CR_SPI_PRD_D_PH_1_SHIFT;
    const auto clock=bflb_clk_get_peripheral_clock(BFLB_DEVICE_TYPE_SPI,spi->idx);
    const auto required=SPI_CR_SPI_M_EN|SPI_CR_SPI_SCLK_POL|SPI_CR_SPI_M_CONT_EN|
        ((SPI_DATA_WIDTH_8BIT-1)<<SPI_CR_SPI_FRAME_SIZE_SHIFT);
    const auto mask=SPI_CR_SPI_M_EN|SPI_CR_SPI_S_EN|SPI_CR_SPI_SCLK_POL|SPI_CR_SPI_SCLK_PH|
        SPI_CR_SPI_M_CONT_EN|SPI_CR_SPI_BIT_INV|SPI_CR_SPI_BYTE_INV|SPI_CR_SPI_FRAME_SIZE_MASK;
    if(!continuous||(stats.config&mask)!=required||p0!=p1||clock!=tc_jtag_spi_hz*(p0+p1+2)){
        fail(TcJtagSpiError::Clock);tc_jtag_spi_close();return false;
    }
    healthy=true;return true;
}

bool tc_jtag_spi_send(const std::uint8_t *data,std::size_t size,bool last){
    if(!owned||!healthy||finished||!data||!size||size>8192)return fail(TcJtagSpiError::State);
    if(!native())return fail(TcJtagSpiError::NativePin);
    if(!muxed&&!attach())return false;
    const auto n=size-(last?1:0);
    if(n&&!send_dma(data,n))return false;
    if(last){
        bflb_mtimer_delay_us(2);park_pins();
        // Exactly eight final clocks. TDI changes while TCK is low, TMS=1 only
        // on the last rising edge. Slow GPIO tail gives >=1 us setup/hold.
        for(int bit=7;bit>=0;--bit){
            drive(1,false);drive(3,(data[size-1]>>bit)&1);drive(0,bit==0);
            bflb_mtimer_delay_us(1);drive(1,true);bflb_mtimer_delay_us(1);
        }
        last_tdi=data[size-1]&1;++stats.gpio_bytes;finished=true;
    }
    return true;
}

bool tc_jtag_spi_close(){
    if(!owned)return true;
    const bool stopped=stop_dma();
    park_pins();
    sw(SPI_CONFIG_OFFSET,sr(SPI_CONFIG_OFFSET)&~std::uint32_t(SPI_CR_SPI_M_EN|SPI_CR_SPI_S_EN));
    if(!stopped)return false; // Never restore live DMA descriptors or release ownership.
    sw(SPI_FIFO_CONFIG_0_OFFSET,SPI_TX_FIFO_CLR|SPI_RX_FIFO_CLR);
    for(unsigned i=1;i<=3;++i)sw(SpiOffsets[i],saved_spi[i]);
    sw(SPI_FIFO_CONFIG_0_OFFSET,saved_spi[4]&(SPI_DMA_TX_EN|SPI_DMA_RX_EN));
    sw(SPI_FIFO_CONFIG_1_OFFSET,saved_spi[5]&(SPI_TX_FIFO_TH_MASK|SPI_RX_FIFO_TH_MASK));
    sw(SPI_INT_STS_OFFSET,saved_spi[6]&0x3f003f00u);
    putreg32((getreg32(Role)&~(1u<<12))|saved_role,Role);
    sw(SPI_CONFIG_OFFSET,saved_spi[0]);
    clear_status();
    for(unsigned i=0;i<5;++i){putreg32(saved_dma[0][i],tx->reg_base+DmaOffsets[i]);putreg32(saved_dma[1][i],rx->reg_base+DmaOffsets[i]);}
    // Preserve the global DMA enable when another channel is active.
    if(!saved_enable&&!getreg32(DmaBase+DMA_ENBLDCHNS_OFFSET))
        putreg32(getreg32(DmaBase+DMA_TOP_CONFIG_OFFSET)&~std::uint32_t(DMA_E),DmaBase+DMA_TOP_CONFIG_OFFSET);
    owned=false;healthy=false;return true;
}
#endif
