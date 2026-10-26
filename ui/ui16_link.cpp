#include "ui16_link.h"
#ifdef UI16_RELIABLE_FONT
#include "utils.h"

namespace {
std::uint32_t (*clock_ms)()=nullptr;
void (*wait_ms)()=nullptr;
volatile std::uint32_t received_seq=0,received_digest=0;
volatile std::uint8_t received_status=0xff;
volatile bool received=false;
std::uint32_t next_seq=1;
bool connected=false;
bool incompatible=false;
std::uint32_t retry_after=0;
std::uint32_t nonce_counter=0;
std::uint32_t crc_byte(std::uint32_t crc,std::uint8_t byte) {
    crc^=byte;
    for(unsigned i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
    return crc;
}
std::uint32_t be32(const std::uint8_t *p) {
    return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|
           (std::uint32_t(p[2])<<8)|p[3];
}
bool exchange(std::uint8_t command,const std::uint8_t *payload,std::size_t count) {
    if(!clock_ms || !wait_ms || count>68)return false;
    const auto length=static_cast<std::uint16_t>(count+10);
    auto crc=crc_byte(crc_byte(crc_byte(0xffffffffu,std::uint8_t(length>>8)),
                           std::uint8_t(length)),0x15);
    for(int i=3;i>=0;--i)crc=crc_byte(crc,std::uint8_t(next_seq>>(8*i)));
    crc=crc_byte(crc,command);
    for(std::size_t i=0;i<count;++i)crc=crc_byte(crc,payload[i]);
    const auto digest=crc^0xffffffffu;
    for(unsigned attempt=0;attempt<3;++attempt) {
        taskENTER_CRITICAL();
        received=false;
        fpga_tx_header(0x15,length);
        for(int i=3;i>=0;--i)fpga_tx_byte(std::uint8_t(next_seq>>(8*i)));
        fpga_tx_byte(command);
        for(std::size_t i=0;i<count;++i)fpga_tx_byte(payload[i]);
        for(int i=0;i<4;++i)fpga_tx_byte(std::uint8_t(digest>>(8*i)));
        taskEXIT_CRITICAL();
        const auto start=clock_ms();
        while(std::uint32_t(clock_ms()-start)<30) {
            bool found;
            std::uint32_t seq,ack_digest;
            std::uint8_t status;
            taskENTER_CRITICAL();
            found=received;seq=received_seq;ack_digest=received_digest;status=received_status;
            if(found)received=false;
            taskEXIT_CRITICAL();
            if(found && seq==next_seq && ack_digest==digest) {
                if(status==0){++next_seq;return true;}
                connected=false;
                if(command==0 || status==3 || status==4)incompatible=true;
                return false;
            }
            wait_ms();
        }
    }
    connected=false;
    if(clock_ms)retry_after=clock_ms()+1000;
    return false;
}
}
void ui16_link_set_platform(std::uint32_t (*millis)(),void (*wait_one_ms)()) {
    clock_ms=millis;wait_ms=wait_one_ms;
}
void ui16_link_monitor_reset() {
    taskENTER_CRITICAL();received=false;taskEXIT_CRITICAL();
    connected=false;incompatible=false;
    retry_after=0;
    next_seq=1+(++nonce_counter<<16);
}
void ui16_link_accept_ack(const std::uint8_t *body,std::size_t length) {
    if(!body || length!=14 || body[0]!=2)return;
    auto crc=crc_byte(crc_byte(crc_byte(0xffffffffu,0),15),0x16);
    for(std::size_t i=0;i<10;++i)crc=crc_byte(crc,body[i]);
    crc^=0xffffffffu;
    if(std::uint8_t(crc)!=body[10] || std::uint8_t(crc>>8)!=body[11] ||
       std::uint8_t(crc>>16)!=body[12] || std::uint8_t(crc>>24)!=body[13])return;
    taskENTER_CRITICAL();
    received_seq=be32(body+2);received_digest=be32(body+6);
    received_status=body[1];received=true;
    taskEXIT_CRITICAL();
}
bool ui16_link_prepare() {
    if(incompatible)return false;
    if(connected)return true;
    if(clock_ms && std::int32_t(clock_ms()-retry_after)<0)return false;
    const std::uint32_t nonce=(clock_ms?clock_ms():0)^(++nonce_counter*0x9e3779b9u);
    const std::uint8_t hello[12]={0x55,0x49,0x31,0x36,2,0x10,0x3f,0x01,
        std::uint8_t(nonce>>24),std::uint8_t(nonce>>16),std::uint8_t(nonce>>8),std::uint8_t(nonce)};
    if(!exchange(0,hello,sizeof hello))return false;
    connected=true;
    return true;
}
bool ui16_link_send(std::uint8_t command,const std::uint8_t *payload,std::size_t length) {
    if(!ui16_link_prepare())return false;
    return exchange(command,payload,length);
}
#endif
