#pragma once
#include <cstddef>
#include <cstdint>
#include "tc_rom_profile.h"

// Transport-independent ROM transaction. UART and a future SPI adapter exchange
// the same checksummed messages; neither the codec nor Session knows baud/pins.
namespace TcRom {
constexpr std::size_t MaxData=1024, Overhead=24, MaxFrame=MaxData+Overhead;
constexpr std::uint8_t Request=0x30, Reply=0x31, Version=1;
enum class Op:std::uint8_t {Begin=1,Data=2,End=3,Start=4,Abort=5,Window=6,LinkRate=0x10,LinkSync=0x11};
enum class Status:std::uint8_t {Ok=0,Format=1,Session=2,Order=3,Range=4,State=5,Busy=6,Integrity=7,Backend=8};
struct View {
    std::uint8_t command=0;
    Op op=Op::Abort;
    std::uint32_t session=0,sequence=0,offset=0;
    const std::uint8_t *data=nullptr;
    std::uint16_t count=0;
};
std::uint32_t crc_update(std::uint32_t state,const void *data,std::size_t size);
std::uint32_t get32(const std::uint8_t *p);
void put32(std::uint8_t *p,std::uint32_t value);
std::size_t encode(std::uint8_t *out,std::size_t capacity,const View &frame,Profile *profile=nullptr);
bool decode(const std::uint8_t *bytes,std::size_t length,View &frame,Profile *profile=nullptr);
struct Transport {
    void *context=nullptr;
    // The caller owns request storage until this returns. A pump may prepare
    // the next source buffer while the transport's DMA transfer is in flight.
    bool (*exchange)(void *,const std::uint8_t *,std::size_t,std::uint8_t *,std::size_t *,
                     std::uint32_t,void (*pump)(void *),void *)=nullptr;
    // Optional full-duplex transport. send retains ownership until all bytes
    // have left DMA/the physical transmitter; it never discards incoming ACKs.
    bool (*send)(void *,const std::uint8_t *,std::size_t,std::uint32_t,
                 void (*pump)(void *),void *)=nullptr;
    bool (*receive)(void *,std::uint8_t *,std::size_t *,std::uint32_t)=nullptr;
    Profile *profile=nullptr;
    // Optional nonblocking reply collection, including partial-frame retention.
    // May run inside send's pump; never flushes or sends another request.
    // send invokes its pump after starting and again on RX/progress events,
    // retaining the request bytes until all DMA/physical access has stopped.
    bool (*poll)(void *,std::uint8_t *,std::size_t *)=nullptr;
};
struct Source {
    void *context=nullptr;
    bool (*read)(void *,std::uint32_t,std::uint8_t *,std::uint16_t,std::uint16_t *)=nullptr;
    void (*pump)(void *)=nullptr;
};
struct Stats {
    std::uint32_t requests=0,retries=0,blocks=0,bytes=0,crc=0,window=1;
    std::uint32_t pipeline=0,pumps=0,prepared_in_send=0,acks_in_send=0,crc_in_send=0;
};
class Session {
public:
    Session(Transport transport,std::uint32_t id,std::uint8_t *storage,std::size_t capacity):
        transport_(transport),id_(id),storage_(storage),capacity_(capacity){}
    bool begin(std::uint32_t total);
    bool stream(Source source);
    bool negotiate_window();
    bool finish();
    bool start();
    bool link_rate(std::uint32_t baud);
    bool link_sync();
    void abort();
    const Stats &stats()const{return stats_;}
private:
    bool request(Op,const std::uint8_t *,std::uint16_t,std::uint32_t timeout=50,
                 void (*pump)(void *)=nullptr,void *pump_context=nullptr,bool any_offset=false);
    bool stream_window(Source);
    struct Pipeline;
    bool stream_pipeline(Source);
    Status last_status_=Status::Busy;
    bool window_two_=false;
    Transport transport_;
    std::uint32_t id_,total_=0,offset_=0,sequence_=0,crc_=0xffffffffu;
    std::uint8_t *storage_;
    std::size_t capacity_;
    bool begun_=false,verified_=false,running_=false;
    Stats stats_{};
};
}
