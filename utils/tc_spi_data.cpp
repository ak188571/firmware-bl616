#ifdef TC_ROM_SPI
#include "tc_spi_data.h"
#include "utils.h"
extern "C" {
#include "bflb_spi.h"
#include "bflb_dma.h"
#include "bflb_clock.h"
#include "hardware/spi_reg.h"
#include "hardware/gpio_reg.h"
}
namespace {
constexpr unsigned offsets[] = {SPI_CONFIG_OFFSET, SPI_PRD_0_OFFSET, SPI_PRD_1_OFFSET,
    SPI_RXD_IGNR_OFFSET, SPI_FIFO_CONFIG_0_OFFSET, SPI_FIFO_CONFIG_1_OFFSET, SPI_INT_STS_OFFSET};
bflb_device_s *device, *tx_dma, *rx_dma;
static ATTR_NOCACHE_NOINIT_RAM_SECTION bflb_dma_channel_lli_pool_s tx_lli[1] __attribute__((aligned(64)));
static ATTR_NOCACHE_NOINIT_RAM_SECTION bflb_dma_channel_lli_pool_s rx_lli[1] __attribute__((aligned(64)));
static ATTR_NOCACHE_NOINIT_RAM_SECTION std::uint8_t rx_discard __attribute__((aligned(64)));
bool prepare_dma(){
    tx_dma=bflb_device_get_by_name("dma0_ch1");
    rx_dma=bflb_device_get_by_name("dma0_ch2");
    if(!tx_dma || !rx_dma || bflb_dma_channel_isbusy(tx_dma) || bflb_dma_channel_isbusy(rx_dma))return false;
    bflb_dma_channel_config_s config{};
    config.direction=DMA_MEMORY_TO_PERIPH;
    config.src_req=DMA_REQUEST_NONE;config.dst_req=DMA_REQUEST_SPI0_TX;
    config.src_addr_inc=DMA_ADDR_INCREMENT_ENABLE;config.dst_addr_inc=DMA_ADDR_INCREMENT_DISABLE;
    config.src_burst_count=DMA_BURST_INCR1;config.dst_burst_count=DMA_BURST_INCR1;
    config.src_width=DMA_DATA_WIDTH_8BIT;config.dst_width=DMA_DATA_WIDTH_8BIT;
    bflb_dma_channel_init(tx_dma,&config);bflb_dma_channel_tcint_mask(tx_dma,true);
    config.direction=DMA_PERIPH_TO_MEMORY;
    config.src_req=DMA_REQUEST_SPI0_RX;config.dst_req=DMA_REQUEST_NONE;
    config.src_addr_inc=DMA_ADDR_INCREMENT_DISABLE;config.dst_addr_inc=DMA_ADDR_INCREMENT_DISABLE;
    bflb_dma_channel_init(rx_dma,&config);bflb_dma_channel_tcint_mask(rx_dma,true);
    return true;
}
uint32_t saved[7], saved_pins[4], saved_tx, saved_role;
bool active, healthy;
volatile uint32_t &reg(unsigned offset){
    return *reinterpret_cast<volatile uint32_t *>(device->reg_base + offset);
}
volatile uint32_t &pin(unsigned n){
    return *reinterpret_cast<volatile uint32_t *>(gpio_dev->reg_base + GLB_GPIO_CFG0_OFFSET + n * 4);
}
// GLB master-role selector used by the BL616 SDK.
volatile uint32_t &role = *reinterpret_cast<volatile uint32_t *>(0x20000510u);
void drive(unsigned n, bool high){
    pin(n) = GLB_REG_GPIO_0_INT_MASK | GLB_REG_GPIO_0_OE | GLB_REG_GPIO_0_SMT |
        (1u << GLB_REG_GPIO_0_MODE_SHIFT) | (11u << GLB_REG_GPIO_0_FUNC_SEL_SHIFT) |
        (high ? GLB_REG_GPIO_0_SET : GLB_REG_GPIO_0_CLR);
}
}
bool tc_spi_open(){
    device = bflb_device_get_by_name("spi0");
    if(active || !device || !gpio_dev)return false;
    for(unsigned i=0;i<7;++i)saved[i]=reg(offsets[i]);
    if((saved[0] & (SPI_CR_SPI_M_EN | SPI_CR_SPI_S_EN)) ||
       (reg(SPI_BUS_BUSY_OFFSET) & SPI_STS_SPI_BUS_BUSY) ||
       (saved[4] & (SPI_DMA_TX_EN | SPI_DMA_RX_EN)) || (saved[5] & SPI_RX_FIFO_CNT_MASK) ||
       ((saved[5] & SPI_TX_FIFO_CNT_MASK) >> SPI_TX_FIFO_CNT_SHIFT) != SPI_FIFO_BYTE_NUM_MAX)return false;
    if(!prepare_dma())return false;
    for(unsigned i=0;i<4;++i)saved_pins[i]=pin(i);
    saved_tx=pin(28); saved_role=role & (1u<<12);
    active=true;
    drive(28,false);
    bflb_mtimer_delay_us(12); // FPGA qualifies the static level before repurposing the pins.
    drive(0,true);
    reg(SPI_INT_STS_OFFSET)=0x3f00u;
    const bflb_spi_config_s cfg={tc_spi_data_hz,SPI_ROLE_MASTER,SPI_MODE1,SPI_DATA_WIDTH_8BIT,
        SPI_BIT_MSB,SPI_BYTE_LSB,0,0};
    bflb_spi_init(device,&cfg);
    // The SDK rounds both phases to the same length. A 160 MHz source
    // needs two clocks in phase 0 and three in phase 1 for exactly 32 MHz.
    constexpr auto phase_mask=SPI_CR_SPI_PRD_D_PH_0_MASK|SPI_CR_SPI_PRD_D_PH_1_MASK;
    constexpr auto phases=(1u<<SPI_CR_SPI_PRD_D_PH_0_SHIFT)|(2u<<SPI_CR_SPI_PRD_D_PH_1_SHIFT);
    reg(SPI_PRD_0_OFFSET)=(reg(SPI_PRD_0_OFFSET)&~phase_mask)|phases;
    if(bflb_spi_feature_control(device,SPI_CMD_SET_CS_INTERVAL,true)!=0 ||
       !(reg(SPI_CONFIG_OFFSET)&SPI_CR_SPI_M_CONT_EN)){
        tc_spi_close();return false;
    }
    const auto period=reg(SPI_PRD_0_OFFSET);
    const auto divisor=2u+((period & SPI_CR_SPI_PRD_D_PH_0_MASK)>>SPI_CR_SPI_PRD_D_PH_0_SHIFT)
        +((period & SPI_CR_SPI_PRD_D_PH_1_MASK)>>SPI_CR_SPI_PRD_D_PH_1_SHIFT);
    if((period&phase_mask)!=phases ||
       bflb_clk_get_peripheral_clock(BFLB_DEVICE_TYPE_SPI,device->idx)/divisor!=tc_spi_data_hz){
        tc_spi_close();return false;
    }
    for(unsigned i=1;i<4;++i)
        bflb_gpio_init(gpio_dev,i,GPIO_FUNC_SPI0|GPIO_ALTERNATE|GPIO_PULLUP|GPIO_SMT_EN|GPIO_DRV_1);
    bflb_mtimer_delay_us(2);
    healthy=true;
    return true;
}
bool tc_spi_send(const std::uint8_t *frame,std::size_t size,std::uint32_t timeout_ms,
                 void (*pump)(void *),void *pump_context){
    if(!active || !healthy || !frame || !size || size>1048)return false;
    bflb_dma_channel_lli_transfer_s tx{},rx{};
    tx.src_addr=reinterpret_cast<std::uintptr_t>(frame);
    tx.dst_addr=DMA_ADDR_SPI0_TDR;tx.nbytes=size;
    rx.src_addr=DMA_ADDR_SPI0_RDR;
    rx.dst_addr=reinterpret_cast<std::uintptr_t>(&rx_discard);rx.nbytes=size;
    bflb_dma_channel_tcint_clear(tx_dma);bflb_dma_channel_tcint_clear(rx_dma);
    if(bflb_dma_channel_lli_reload(tx_dma,tx_lli,1,&tx,1)!=1 ||
       bflb_dma_channel_lli_reload(rx_dma,rx_lli,1,&rx,1)!=1){
        healthy=false;return false;
    }
    const auto deadline=bflb_mtimer_get_time_us()+1000ull*timeout_ms;
    drive(0,false);bflb_mtimer_delay_us(2);
    bflb_spi_link_rxdma(device,true);bflb_spi_link_txdma(device,true);
    // RX must drain from the first clock even while CPU prefetch/CRC work runs.
    bflb_dma_channel_start(rx_dma);bflb_dma_channel_start(tx_dma);
    if(pump)pump(pump_context);
    while(bflb_dma_channel_isbusy(tx_dma) || bflb_dma_channel_isbusy(rx_dma) ||
          (reg(SPI_BUS_BUSY_OFFSET)&SPI_STS_SPI_BUS_BUSY)){
        if((reg(SPI_FIFO_CONFIG_0_OFFSET)&0xf0u) || bflb_mtimer_get_time_us()>=deadline){
            healthy=false;break;
        }
    }
    bflb_spi_link_txdma(device,false);bflb_spi_link_rxdma(device,false);
    bflb_dma_channel_stop(tx_dma);bflb_dma_channel_stop(rx_dma);
    if((reg(SPI_FIFO_CONFIG_0_OFFSET)&0xf0u) ||
       (reg(SPI_FIFO_CONFIG_1_OFFSET)&SPI_RX_FIFO_CNT_MASK))healthy=false;
    if(!healthy)reg(SPI_CONFIG_OFFSET)&=~uint32_t(SPI_CR_SPI_M_EN|SPI_CR_SPI_S_EN);
    bflb_mtimer_delay_us(2);drive(0,true);bflb_mtimer_delay_us(2);
    return healthy;
}
void tc_spi_close(){
    if(!active)return;
    bflb_spi_link_txdma(device,false);bflb_spi_link_rxdma(device,false);
    bflb_dma_channel_stop(tx_dma);bflb_dma_channel_stop(rx_dma);
    reg(SPI_CONFIG_OFFSET)&=~uint32_t(SPI_CR_SPI_M_EN|SPI_CR_SPI_S_EN);
    reg(SPI_FIFO_CONFIG_0_OFFSET)=SPI_TX_FIFO_CLR|SPI_RX_FIFO_CLR;
    reg(SPI_PRD_0_OFFSET)=saved[1];reg(SPI_PRD_1_OFFSET)=saved[2];reg(SPI_RXD_IGNR_OFFSET)=saved[3];
    reg(SPI_FIFO_CONFIG_0_OFFSET)=saved[4]&(SPI_DMA_TX_EN|SPI_DMA_RX_EN);
    reg(SPI_FIFO_CONFIG_1_OFFSET)=saved[5]&(SPI_TX_FIFO_TH_MASK|SPI_RX_FIFO_TH_MASK);
    reg(SPI_INT_STS_OFFSET)=saved[6]&0x3f003f00u;
    role=(role&~(1u<<12))|saved_role;
    reg(SPI_CONFIG_OFFSET)=saved[0];
    for(unsigned i=0;i<4;++i)pin(i)=saved_pins[i]&~uint32_t(GLB_REG_GPIO_0_INT_CLR);
    drive(28,true);bflb_mtimer_delay_us(12);
    pin(28)=saved_tx&~uint32_t(GLB_REG_GPIO_0_INT_CLR);
    healthy=false;active=false;
}
#endif
