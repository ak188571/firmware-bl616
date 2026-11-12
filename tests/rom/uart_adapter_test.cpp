#include "rom_uart_platform.h"
extern "C" {
#include "bflb_dma.h"
}
#include "tc_rom_uart.h"
#include "tc_rom_protocol.h"
#include "tc_launch_trace.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <set>
#include <map>
#include <vector>
using namespace TcRom;
extern "C" int __wrap_bflb_uart_put(bflb_device_s *,std::uint8_t *,std::uint32_t);
extern "C" int __wrap_bflb_uart_putchar(bflb_device_s *,int);
static bflb_device_s uart{1},other{2},dma_dev{3};
bflb_device_s *uart1_dev=&uart;
static std::uint32_t ticks=0,baud=2000000,next_sequence=0,stream_crc=0xffffffffu;
static std::vector<std::uint8_t> input,committed,last,previous;
static std::deque<std::uint8_t> rx;
static bflb_dma_channel_lli_transfer_s transfer{};
static bool pumping=false,verified=false,running=false,aborted=false,fail_dma=false,lose=false,damage=false,all_lost=false;
static unsigned reads=0,starts=0,real_puts=0,critical=0,fail_read=~0u,short_read=~0u;
static std::set<void*> buffers;
static std::uint8_t header[64];
static bool irq_enabled=true;
static bool delay_dma=false,stuck_dma=false,channel_active=false;
static bool old_core=false,window_silent=false;
static unsigned busy_polls=0,busy_events=0;
static std::vector<std::uint8_t> dma_snapshot;
struct Pending {std::uint32_t due;std::vector<std::uint8_t> bytes;};
static std::vector<Pending> pending;
static bool delayed_ack=false,split_ack=false,out_of_order=false,lose_all_data=false;
static std::map<unsigned,unsigned> data_attempts;
static void deliver(){
    for(auto it=pending.begin();it!=pending.end();){if(it->due<=ticks){rx.insert(rx.end(),it->bytes.begin(),it->bytes.end());it=pending.erase(it);}else ++it;}
}
static void pinned_unchanged(){if(channel_active)assert(std::memcmp(reinterpret_cast<const void*>(transfer.src_addr),dma_snapshot.data(),dma_snapshot.size())==0);}
extern "C" std::uintptr_t bflb_irq_save(){const auto old=irq_enabled;irq_enabled=false;return old;}
extern "C" void bflb_irq_restore(std::uintptr_t old){irq_enabled=old;}
extern "C" void mock_enter(){assert(!critical);++critical;}
extern "C" void mock_leave(){assert(critical==1);--critical;}
extern "C" std::uint32_t bflb_mtimer_get_time_ms(){assert(!critical);return ticks++;}
extern "C" std::uint64_t bflb_mtimer_get_time_us(){return std::uint64_t(ticks)*1000;}
#ifdef TC_LAUNCH_TRACE
std::uint64_t tc_launch_now_us(){return bflb_mtimer_get_time_us();}
#endif
void delay(unsigned n){assert(!critical);ticks+=n;}
extern "C" bool bflb_uart_txempty(bflb_device_s *){return true;}
extern "C" void bflb_uart_link_txdma(bflb_device_s *,bool){}
extern "C" int bflb_uart_feature_control(bflb_device_s *,int command,std::uint32_t arg){
    if(command==UART_CMD_SET_BAUD_RATE){assert(arg==2000000||arg==8000000);baud=arg;}
    if(command==UART_CMD_CLR_RX_FIFO)rx.clear();return 0;
}
extern "C" bflb_device_s *bflb_device_get_by_name(const char *name){assert(std::strcmp(name,"dma0_ch3")==0);return &dma_dev;}
extern "C" std::uint32_t getreg32(std::uintptr_t){return 0;}
extern "C" int __real_bflb_uart_put(bflb_device_s *,std::uint8_t *,std::uint32_t n){assert(!critical);++real_puts;return n;}
extern "C" int __real_bflb_uart_putchar(bflb_device_s *,int){assert(!critical);++real_puts;return 0;}
extern "C" bool bflb_dma_channel_isbusy(bflb_device_s *){
    pinned_unchanged();
    deliver();
    if(stuck_dma&&channel_active)return true;
    if(busy_polls){--busy_polls;++busy_events;return true;}
    channel_active=false;return false;
}
extern "C" void bflb_dma_channel_init(bflb_device_s *,bflb_dma_channel_config_s *c){assert(c->dst_req==DMA_REQUEST_UART1_TX&&c->src_addr_inc==DMA_ADDR_INCREMENT_ENABLE);}
extern "C" void bflb_dma_channel_tcint_mask(bflb_device_s *,bool){}
extern "C" void bflb_dma_channel_tcint_clear(bflb_device_s *){}
extern "C" int bflb_dma_channel_lli_reload(bflb_device_s *,bflb_dma_channel_lli_pool_s *,int pool,bflb_dma_channel_lli_transfer_s *t,int count){
    assert(!channel_active&&pool==1&&count==1&&t->nbytes<=MaxFrame);transfer=*t;return fail_dma?0:1;
}
static void accept_wire(const std::uint8_t *wire,std::size_t size){
    if(wire[5]==2)assert(++data_attempts[get32(wire+10)]<=4);
    std::vector<std::uint8_t> frame(wire,wire+size);View f;
    if(damage&&wire[5]==2){frame[20]^=1;damage=false;}
    if(!decode(frame.data(),frame.size(),f))return;
    std::uint8_t status=0;
    if(f.op==Op::Begin){assert(f.count==4);committed.clear();last.clear();previous.clear();next_sequence=0;stream_crc=0xffffffffu;}
    else if(f.op==Op::Data){
        assert(baud==8000000);
        if(frame!=last&&frame!=previous){
            if(f.offset!=committed.size()||f.sequence!=next_sequence)status=3;
            else {
            committed.insert(committed.end(),f.data,f.data+f.count);++next_sequence;
            stream_crc=crc_update(stream_crc,f.data,f.count);previous=last;last=frame;}
        }
    }else if(f.op==Op::End){verified=get32(f.data)==(stream_crc^0xffffffffu);assert(verified);}
    else if(f.op==Op::Window){if(old_core)status=1;if(window_silent)return;}
    else if(f.op==Op::Start){assert(verified&&baud==2000000);running=true;++starts;}
    else if(f.op==Op::Abort){aborted=true;running=false;}
    std::array<std::uint8_t,25> reply{};
    auto n=encode(reply.data(),reply.size(),{Reply,f.op,f.session,f.sequence,f.op==Op::Data&&status==0?f.offset+f.count:std::uint32_t(committed.size()),&status,1});assert(n==25);
    if(all_lost)return;
    if(lose&&f.op==Op::Data){lose=false;return;}
    if(lose_all_data&&f.op==Op::Data)return;
    if((delayed_ack||out_of_order)&&f.op==Op::Data){
        const auto wait=out_of_order&&f.sequence==0?30u:out_of_order?2u:16u;
        const unsigned first=split_ack?7:25;
        pending.push_back({ticks+wait,std::vector<std::uint8_t>(reply.begin(),reply.begin()+first)});
        if(split_ack)pending.push_back({ticks+wait+3,std::vector<std::uint8_t>(reply.begin()+first,reply.end())});
    }else rx.insert(rx.end(),reply.begin(),reply.end());
}
#ifdef TC_ROM_SPI
static bool spi_open=false,spi_fault=false;
static unsigned spi_frames=0;
bool tc_spi_open(){assert(!spi_open && baud==8000000 && tc_rom_uart_owned());spi_open=true;return true;}
void tc_spi_close(){assert(spi_open);spi_open=false;}
bool tc_spi_send(const std::uint8_t *wire,std::size_t size,std::uint32_t,void (*pump)(void*),void *context){
    assert(spi_open && wire[5]==2 && baud==8000000);
    ++spi_frames;
    if(spi_fault)return false;
    const std::vector<std::uint8_t> pinned(wire,wire+size);
    accept_wire(wire,size);
    if(pump)pump(context);
    assert(std::equal(pinned.begin(),pinned.end(),wire));
    return true;
}
#endif
extern "C" void bflb_dma_channel_start(bflb_device_s *){
    assert(tc_rom_uart_owned()&&!critical);pumping=true;channel_active=true;
    if(delay_dma)busy_polls=12;
    const auto *wire=reinterpret_cast<const std::uint8_t*>(transfer.src_addr);
    dma_snapshot.assign(wire,wire+transfer.nbytes);
#ifdef TC_ROM_SPI
    assert(!spi_open);
#endif
    accept_wire(wire,transfer.nbytes);
}
extern "C" void bflb_dma_channel_stop(bflb_device_s *){pinned_unchanged();pumping=false;busy_polls=0;channel_active=false;}
void tc_rom_uart_rx_flush(){rx.clear();}
bool tc_rom_uart_rx_pending(){pinned_unchanged();deliver();return !rx.empty();}
int tc_rom_uart_rx_take(std::uint8_t *b){pinned_unchanged();deliver();if(rx.empty())return 0;*b=rx.front();rx.pop_front();return 1;}
extern "C" FRESULT f_lseek(FIL *file,FSIZE_t at){file->position=at;return at>input.size()?FR_DISK_ERR:FR_OK;}
extern "C" FRESULT f_read(FIL *file,void *buffer,UINT n,UINT *got){
    assert(pumping&&tc_rom_uart_owned()&&!critical&&n<=4096);buffers.insert(buffer);*got=0;
    std::uint8_t b=0;const auto before=real_puts;
    __wrap_bflb_uart_put(&uart,&b,1);__wrap_bflb_uart_putchar(&uart,b);assert(real_puts==before);
    __wrap_bflb_uart_put(&other,&b,1);assert(real_puts==before+1);
    if(reads==fail_read){++reads;return FR_DISK_ERR;}
    *got=std::min<std::size_t>(n,input.size()-file->position);
    if(reads++==short_read&&*got)--*got;
    std::memcpy(buffer,input.data()+file->position,*got);file->position+=*got;return FR_OK;
}
static void reset(unsigned bytes,unsigned prefix){
    input.resize(bytes+prefix);std::uint32_t x=0x2749ab53;
    for(auto &b:input){x^=x<<13;x^=x>>17;x^=x<<5;b=std::uint8_t(x>>24);}
    for(unsigned i=0;i<64;++i)header[i]=std::uint8_t(i*31+5);
    buffers.clear();reads=starts=real_puts=critical=0;baud=2000000;rx.clear();committed.clear();
    pumping=verified=running=aborted=fail_dma=lose=damage=all_lost=false;fail_read=short_read=~0u;
    delay_dma=stuck_dma=channel_active=false;busy_polls=busy_events=0;
    old_core=window_silent=false;
    delayed_ack=split_ack=out_of_order=lose_all_data=false;pending.clear();data_attempts.clear();
    tc_rom_uart_capabilities(true,true);assert(tc_rom_uart_capable()&&!tc_rom_uart_owned());
    tc_launch_begin();
}
int main(){
    irq_enabled=false;__wrap_bflb_uart_putchar(&uart,0x1234);assert(!irq_enabled);
    irq_enabled=true;__wrap_bflb_uart_putchar(&uart,0x1234);assert(irq_enabled);
    for(unsigned bytes:{1024u,8192u,2u*1024*1024,4u*1024*1024})for(unsigned prefix:{0u,512u}){
        reset(bytes,prefix);lose=damage=true;FIL file{0,input.size()};
        assert(tc_rom_uart_load_snes(file,prefix,bytes,header)&&running&&starts==1&&baud==2000000&&!tc_rom_uart_owned());
        assert(committed.size()==bytes+64&&std::equal(committed.begin(),committed.begin()+64,header));
        assert(std::equal(committed.begin()+64,committed.end(),input.begin()+prefix));
        assert(reads==(bytes+4095)/4096&&buffers.size()==(bytes<=4096?1u:2u));
#ifdef TC_LAUNCH_TRACE
        assert(tc_launch_record.rom_baud==8000000&&tc_launch_record.retries>=2&&tc_launch_record.rom_bytes==bytes);
        assert(tc_launch_record.end_ack_us>0&&tc_launch_record.stream_crc==(stream_crc^0xffffffffu));
        assert(tc_launch_record.rom_pipeline==1&&tc_launch_record.cpu_pumps>0&&tc_launch_record.cpu_prepared>0);
#ifdef TC_ROM_PROFILE
        assert(tc_launch_record.profile_enabled&&!tc_launch_record.profile_error);
        const auto &p=tc_launch_record.profile[0];std::uint64_t sum=0;for(auto n:p.us)sum+=n;
        assert(sum==p.elapsed_us&&p.tx_frames&&p.tx_bytes&&p.ack_calls);
        assert(p.units[static_cast<unsigned>(Activity::StreamCrc)]==bytes+64);
#endif
#endif
    }
    reset(8192,0);delay_dma=true;FIL delayed{0,input.size()};
    assert(tc_rom_uart_load_snes(delayed,0,8192,header)&&running&&busy_events>0&&!busy_polls);
    for(unsigned mode=0;mode<3;++mode){
        reset(16384,0);delay_dma=true;delayed_ack=true;split_ack=mode!=0;out_of_order=mode==2;
        FIL file{0,input.size()};assert(tc_rom_uart_load_snes(file,0,16384,header)&&running&&committed.size()==16448);
#ifdef TC_LAUNCH_TRACE
        assert(tc_launch_record.cpu_prepared>0&&tc_launch_record.cpu_acks>0&&tc_launch_record.cpu_crc>0);
#endif
    }
    reset(16384,0);lose_all_data=true;FIL lost{0,input.size()};
    assert(!tc_rom_uart_load_snes(lost,0,16384,header)&&!running&&data_attempts.size()==2);
    for(auto row:data_attempts)assert(row.second==4);
    reset(8192,0);old_core=true;FIL legacy{0,input.size()};
    assert(tc_rom_uart_load_snes(legacy,0,8192,header)&&running&&committed.size()==8256);
#ifdef TC_LAUNCH_TRACE
    assert(tc_launch_record.rom_window==1);
    assert(tc_launch_record.rom_pipeline==0);
#endif
    reset(8192,0);window_silent=true;FIL silent{0,input.size()};
    assert(!tc_rom_uart_load_snes(silent,0,8192,header)&&!running&&committed.empty());
    for(unsigned fault=0;fault<6;++fault){
        reset(16384,0);FIL file{0,input.size()};
        if(fault==0)fail_read=0;if(fault==1)fail_read=3;if(fault==2)short_read=2;if(fault==3)fail_dma=true;if(fault==4)all_lost=true;if(fault==5)stuck_dma=true;
        assert(!tc_rom_uart_load_snes(file,0,16384,header)&&!running&&!starts&&!tc_rom_uart_owned()&&baud==2000000);
        assert(!channel_active);
    }
#ifdef TC_ROM_SPI
    assert(spi_frames==0);
    for(unsigned bytes:{2u*1024*1024,4u*1024*1024}){
        reset(bytes,512);tc_rom_uart_capabilities(true,true,true);spi_frames=0;lose=damage=true;
        FIL file{0,input.size()};
        assert(tc_rom_uart_load_snes(file,512,bytes,header) && starts==1 && !spi_open);
        assert(spi_frames>bytes/1024 && committed.size()==bytes+64);
        assert(std::equal(committed.begin(),committed.begin()+64,header));
        assert(std::equal(committed.begin()+64,committed.end(),input.begin()+512));
    }
    reset(8192,0);tc_rom_uart_capabilities(true,true,true);spi_fault=true;
    FIL faulty{0,input.size()};
    assert(!tc_rom_uart_load_snes(faulty,0,8192,header) && !spi_open && !starts && !tc_rom_uart_owned());
    std::puts("PASS real loader with mock SPI: 2/4 MiB, copier header, UART control, retries, SPI fault cleanup");
#endif
    tc_rom_uart_capabilities(true,false);assert(!tc_rom_uart_capable());
    std::puts("Actual UART adapter: DMA framing, two 4 KiB FatFs buffers, 2/4 MiB and copier512, CRC/ACK retry, printf exclusion, short/read/DMA/no-ACK failures passed");
}
