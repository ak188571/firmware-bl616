#include "tc_rom_uart.h"
#ifdef TC_ROM_UART
#include "tc_rom_protocol.h"
#include "tc_launch_trace.h"
#include "utils.h"
#include <cstring>
extern "C" {
#include "bflb_dma.h"
#include "bflb_irq.h"
#include "hardware/uart_reg.h"
}
namespace {
volatile bool exclusive=false,capable=false;
volatile unsigned legacy_writers=0;
bool acquire(){
    const auto started=bflb_mtimer_get_time_ms();
    do{
        const auto irq=bflb_irq_save();
        const bool free=legacy_writers==0&&!exclusive;
        if(free)exclusive=true;
        bflb_irq_restore(irq);
        if(free)return true;
        delay(1);
    }while(bflb_mtimer_get_time_ms()-started<500);
    return false;
}
static ATTR_NOCACHE_NOINIT_RAM_SECTION std::uint8_t source_buffers[2][4096] __attribute__((aligned(64)));
static ATTR_NOCACHE_NOINIT_RAM_SECTION std::uint8_t frame_storage[2*TcRom::MaxFrame] __attribute__((aligned(64)));
static ATTR_NOCACHE_NOINIT_RAM_SECTION bflb_dma_channel_lli_pool_s lli[1] __attribute__((aligned(64)));
bflb_device_s *dma=nullptr;
std::uint32_t session_counter=0;
bool drained(std::uint32_t timeout_ms=50){
    const auto start=bflb_mtimer_get_time_ms();
    while(!bflb_uart_txempty(uart1_dev)||(getreg32(uart1_dev->reg_base+UART_STATUS_OFFSET)&UART_STS_UTX_BUS_BUSY))
        if(bflb_mtimer_get_time_ms()-start>=timeout_ms)return false;
    return true;
}
bool set_rate(std::uint32_t baud){
    if((baud!=2000000&&baud!=8000000)||!drained())return false;
    bflb_uart_link_txdma(uart1_dev,false);
    if(bflb_uart_feature_control(uart1_dev,UART_CMD_SET_BAUD_RATE,baud)!=0)return false;
    bflb_uart_feature_control(uart1_dev,UART_CMD_CLR_RX_FIFO,0);tc_rom_uart_rx_flush();return true;
}
bool prepare_dma(){
    if(!dma){
        dma=bflb_device_get_by_name("dma0_ch3");
        if(!dma||bflb_dma_channel_isbusy(dma)){dma=nullptr;return false;}
        bflb_dma_channel_config_s config{};
        config.direction=DMA_MEMORY_TO_PERIPH;config.src_req=DMA_REQUEST_NONE;config.dst_req=DMA_REQUEST_UART1_TX;
        config.src_addr_inc=DMA_ADDR_INCREMENT_ENABLE;config.dst_addr_inc=DMA_ADDR_INCREMENT_DISABLE;
        config.src_burst_count=DMA_BURST_INCR1;config.dst_burst_count=DMA_BURST_INCR1;
        config.src_width=DMA_DATA_WIDTH_8BIT;config.dst_width=DMA_DATA_WIDTH_8BIT;
        bflb_dma_channel_init(dma,&config);bflb_dma_channel_tcint_mask(dma,true);
    }
    return !bflb_dma_channel_isbusy(dma);
}
bool send_frame(void *,const std::uint8_t *request,std::size_t size,std::uint32_t timeout,
                void (*pump)(void *),void *pump_context){
    if(!exclusive||!dma||size>TcRom::MaxFrame||!drained())return false;
    bflb_dma_channel_lli_transfer_s transfer{};
    transfer.src_addr=reinterpret_cast<std::uintptr_t>(request);transfer.dst_addr=DMA_ADDR_UART1_TDR;transfer.nbytes=size;
    const auto enqueue_start=tc_launch_clock();
    bflb_dma_channel_tcint_clear(dma);
    if(bflb_dma_channel_lli_reload(dma,lli,1,&transfer,1)!=1)return false;
    bflb_uart_link_txdma(uart1_dev,true);bflb_dma_channel_start(dma);
#ifdef TC_LAUNCH_TRACE
    if(tc_launch_record.active)tc_launch_record.uart_enqueue_us+=tc_launch_clock()-enqueue_start;
#else
    (void)enqueue_start;
#endif
    const auto start=bflb_mtimer_get_time_ms();
    if(pump)pump(pump_context);
    while(bflb_dma_channel_isbusy(dma)&&bflb_mtimer_get_time_ms()-start<timeout){}
    if(!bflb_dma_channel_isbusy(dma)&&drained())return true;
    bflb_dma_channel_stop(dma);bflb_uart_link_txdma(uart1_dev,false);
    bflb_uart_feature_control(uart1_dev,UART_CMD_CLR_TX_FIFO,0);return false;
}
bool receive_frame(void *,std::uint8_t *reply,std::size_t *reply_size,std::uint32_t timeout){
    if(*reply_size<25)return false;
    const auto start=bflb_mtimer_get_time_ms();
    std::uint8_t window[25]{};unsigned used=0;
    while(bflb_mtimer_get_time_ms()-start<timeout){
        std::uint8_t byte=0;const int got=tc_rom_uart_rx_take(&byte);
        if(got<0){used=0;continue;}if(!got)continue;
        if(used==sizeof window){std::memmove(window,window+1,sizeof window-1);--used;}
        window[used++]=byte;
        TcRom::View answer;
        if(used==sizeof window&&TcRom::decode(window,sizeof window,answer)&&answer.command==TcRom::Reply&&answer.count==1){
            std::memcpy(reply,window,sizeof window);*reply_size=sizeof window;return true;
        }
    }
    return false;
}
bool exchange(void *context,const std::uint8_t *request,std::size_t size,std::uint8_t *reply,std::size_t *reply_size,
              std::uint32_t timeout,void (*pump)(void *),void *pump_context){
    tc_rom_uart_rx_flush();
    if(!send_frame(context,request,size,timeout,pump,pump_context))return false;
    const auto start=bflb_mtimer_get_time_ms();
    while(bflb_mtimer_get_time_ms()-start<timeout){
        if(!receive_frame(context,reply,reply_size,timeout))return false;
        TcRom::View answer;
        if(TcRom::decode(reply,*reply_size,answer)&&static_cast<std::uint8_t>(answer.op)==request[5]&&
           answer.session==TcRom::get32(request+6)&&answer.sequence==TcRom::get32(request+10))return true;
    }
    return false;
}
struct Stream {
    FIL *file=nullptr;
    std::uint8_t header[64]{};
    std::uint32_t remaining=0,delivered=0;
    unsigned current=0,fill=0;
    UINT used[2]{},available[2]{};
    bool ready[2]{},failed=false;
    void prefetch(){
        if(failed||!remaining||ready[fill])return;
        const UINT wanted=remaining<sizeof source_buffers[0]?remaining:sizeof source_buffers[0];
        UINT got=0;const auto begin=tc_launch_clock();
        const auto result=f_read(file,source_buffers[fill],wanted,&got);
#ifdef TC_LAUNCH_TRACE
        if(tc_launch_record.active)tc_launch_record.sd_read_us+=tc_launch_clock()-begin;
#else
        (void)begin;
#endif
        if(result!=FR_OK||got!=wanted){failed=true;return;}
        used[fill]=0;available[fill]=got;ready[fill]=true;remaining-=got;fill^=1;
    }
    static void pump(void *p){static_cast<Stream*>(p)->prefetch();}
    static bool read(void *p,std::uint32_t offset,std::uint8_t *out,std::uint16_t maximum,std::uint16_t *n){
        auto &s=*static_cast<Stream*>(p);*n=0;
        if(s.failed||offset!=s.delivered)return false;
        if(!offset){if(maximum<64)return false;std::memcpy(out,s.header,64);*n=64;s.delivered=64;return true;}
        if(!s.ready[s.current])s.prefetch();
        if(s.failed||!s.ready[s.current])return false;
        const auto left=s.available[s.current]-s.used[s.current];
        *n=static_cast<std::uint16_t>(left<maximum?left:maximum);
        std::memcpy(out,source_buffers[s.current]+s.used[s.current],*n);
        s.used[s.current]+=*n;s.delivered+=*n;
        if(s.used[s.current]==s.available[s.current]){s.ready[s.current]=false;s.current^=1;}
        tc_launch_block(*n,0,0);return true;
    }
};
}
bool tc_rom_uart_owned(){return exclusive;}
void tc_rom_uart_capabilities(bool protocol,bool uart8){capable=protocol&&uart8;}
bool tc_rom_uart_capable(){return capable;}
bool tc_rom_uart_load_snes(FIL &file,std::uint32_t off,std::uint32_t bytes,const std::uint8_t *header){
    if(!capable||!header||!bytes||(bytes&1)||bytes>0x7e0000||!prepare_dma()||!drained())return false;
    Stream source;source.file=&file;source.remaining=bytes;std::memcpy(source.header,header,64);
    if(f_lseek(&file,off)!=FR_OK)return false;
    if(!acquire())return false;
    tc_rom_uart_rx_flush();
    auto id=static_cast<std::uint32_t>(bflb_mtimer_get_time_us())^(++session_counter*0x9e3779b9u);
    if(!id)id=1;
    TcRom::Session session({nullptr,exchange,send_frame,receive_frame},id,frame_storage,sizeof frame_storage);
#ifdef TC_LAUNCH_TRACE
    if(tc_launch_record.active)tc_launch_record.rom_baud=8000000;
#endif
    tc_launch_mark(TcLaunchEvent::RomBegin);
    bool end_started=false;
    bool success=session.begin(bytes+64);
    if(success)success=session.link_rate(8000000);
    if(success){delay(6);success=set_rate(8000000)&&session.link_sync();}
    if(success)success=session.negotiate_window();
    if(success)success=session.stream({&source,Stream::read,Stream::pump});
    if(success)tc_launch_mark(TcLaunchEvent::RomQueued);
    if(success){end_started=true;success=session.finish();}
#ifdef TC_LAUNCH_TRACE
    if(success&&tc_launch_record.active)tc_launch_record.end_ack_us=tc_launch_clock()-tc_launch_record.origin_us;
#endif
    if(success)success=session.link_rate(2000000);
    if(success){delay(6);success=set_rate(2000000)&&session.link_sync();}
    if(success)success=session.start();
    if(success)tc_launch_mark(TcLaunchEvent::ReleaseQueued);
    if(!success)session.abort();
#ifdef TC_LAUNCH_TRACE
    if(tc_launch_record.active){
        tc_launch_record.requests=session.stats().requests;tc_launch_record.retries=session.stats().retries;
        tc_launch_record.stream_crc=session.stats().crc;
        tc_launch_record.rom_window=session.stats().window;
    }
#endif
    bflb_dma_channel_stop(dma);bflb_uart_link_txdma(uart1_dev,false);
    if(!success){
        bflb_uart_feature_control(uart1_dev,UART_CMD_CLR_TX_FIFO,0);
        // A verified receiver keeps END retryable beyond its ten-second ACK
        // timeout. Wait past that watchdog too if END might have completed.
        set_rate(2000000);delay(end_started?12200:1200);
    }
    bflb_uart_feature_control(uart1_dev,UART_CMD_CLR_RX_FIFO,0);tc_rom_uart_rx_flush();
    const auto irq=bflb_irq_save();exclusive=false;bflb_irq_restore(irq);
    return success;
}
// Cover SDK printf/putstring as well as the application's framed writers.
// Count in-flight legacy calls before acquiring the link; no global interrupt
// masking is held across a transfer, source read, or a legacy blocking write.
extern "C" int __real_bflb_uart_put(struct bflb_device_s *,std::uint8_t *,std::uint32_t);
extern "C" int __real_bflb_uart_putchar(struct bflb_device_s *,int);
namespace {
bool legacy_enter(struct bflb_device_s *dev){
    if(dev!=uart1_dev)return true;
    // Preserve the caller's interrupt state, including SDK output from an ISR.
    const auto irq=bflb_irq_save();const bool allowed=!exclusive;if(allowed)++legacy_writers;bflb_irq_restore(irq);
    return allowed;
}
void legacy_leave(struct bflb_device_s *dev){if(dev==uart1_dev){const auto irq=bflb_irq_save();--legacy_writers;bflb_irq_restore(irq);}}
}
extern "C" int __wrap_bflb_uart_put(struct bflb_device_s *dev,std::uint8_t *data,std::uint32_t n){
    if(!legacy_enter(dev))return static_cast<int>(n);
    const int result=__real_bflb_uart_put(dev,data,n);legacy_leave(dev);return result;
}
extern "C" int __wrap_bflb_uart_putchar(struct bflb_device_s *dev,int byte){
    if(!legacy_enter(dev))return 0;
    const int result=__real_bflb_uart_putchar(dev,byte);legacy_leave(dev);return result;
}
#endif
