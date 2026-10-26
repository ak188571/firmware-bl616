#include "ui16_link.h"
#include "utils.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

int critical_depth=0;
void enter_critical(){assert(critical_depth++==0);}
void leave_critical(){assert(--critical_depth==0);}
static std::vector<std::vector<std::uint8_t>> frames;
static std::size_t processed=0;
static std::uint32_t now_ms=100;
static int hello_attempts=0,blank_attempts=0,font_attempts=0;
static bool nack_hello=false;
void fpga_tx_byte(std::uint8_t b){assert(critical_depth==1);frames.back().push_back(b);}
void fpga_tx_header(int command,int length){
    assert(critical_depth==1);
    frames.emplace_back();
    fpga_tx_byte(0xaa);fpga_tx_byte(std::uint8_t(length>>8));
    fpga_tx_byte(std::uint8_t(length));fpga_tx_byte(std::uint8_t(command));
}
static std::uint32_t crc_byte(std::uint32_t c,std::uint8_t b){
    c^=b;
    for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0u);
    return c;
}
static std::uint8_t crc8_byte(std::uint8_t c,std::uint8_t b){
    c^=b;
    for(unsigned i=0;i<8;++i)c=(c&0x80)?std::uint8_t((c<<1)^7):std::uint8_t(c<<1);
    return c;
}
static std::uint32_t be32(const std::uint8_t *p){
    return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|
           (std::uint32_t(p[2])<<8)|p[3];
}
static void reply(const std::vector<std::uint8_t> &frame,bool damage,std::uint8_t status=0){
    const auto seq=be32(frame.data()+4);
    std::uint32_t digest=0;
    for(int i=0;i<4;++i)digest|=std::uint32_t(frame[frame.size()-4+i])<<(8*i);
    std::uint8_t ack[14]={2,status,
        std::uint8_t(seq>>24),std::uint8_t(seq>>16),std::uint8_t(seq>>8),std::uint8_t(seq),
        std::uint8_t(digest>>24),std::uint8_t(digest>>16),std::uint8_t(digest>>8),std::uint8_t(digest)};
    auto crc=crc_byte(crc_byte(crc_byte(0xffffffffu,0),15),0x16);
    for(unsigned i=0;i<10;++i)crc=crc_byte(crc,ack[i]);
    crc^=0xffffffffu;
    for(unsigned i=0;i<4;++i)ack[10+i]=std::uint8_t(crc>>(8*i));
    if(damage)ack[13]^=1;
    ui16_link_accept_ack(ack,sizeof ack);
}
static void wait_one_ms(){
    ++now_ms;
    if(processed==frames.size())return;
    const auto &frame=frames[processed++];
    assert(frame.size()>=13 && frame[0]==0xaa && frame[3]==0x15);
    const auto length=(std::uint16_t(frame[1])<<8)|frame[2];
    assert(frame.size()==std::size_t(length)+3);
    auto crc=0xffffffffu;
    for(std::size_t i=1;i<frame.size()-4;++i)crc=crc_byte(crc,frame[i]);
    crc^=0xffffffffu;
    for(unsigned i=0;i<4;++i)assert(frame[frame.size()-4+i]==std::uint8_t(crc>>(8*i)));
    switch(frame[8]){
    case 0:
        if(nack_hello){reply(frame,false,5);return;}  // incompatible protocol
        if(++hello_attempts==1)return;                 // lost HELLO ACK
        break;
    case 0x13: if(++blank_attempts==1){reply(frame,true);return;}break;
    case 0x12: if(++font_attempts==1)return;break;      // dropped font packet
    default: break;
    }
    reply(frame,false);
}
static std::uint32_t clock_now(){return now_ms;}
int main(int argc,char **argv){
    ui16_link_set_platform(clock_now,wait_one_ms);
    ui16_link_monitor_reset();
    assert(ui16_link_prepare() && hello_attempts==2);
    const std::uint8_t blank[]={0,0};
    assert(ui16_link_send(0x13,blank,sizeof blank) && blank_attempts==2);
    std::uint8_t font[35]={0x3f,1};
    for(unsigned i=0;i<34;++i)font[34]=crc8_byte(font[34],font[i]);
    assert(ui16_link_send(0x12,font,sizeof font) && font_attempts==2);
    std::uint8_t row[68]={};row[2]=32;
    for(unsigned i=0;i<67;++i)row[67]=crc8_byte(row[67],row[i]);
    assert(ui16_link_send(0x14,row,sizeof row));
    const auto &a=frames[0],&b=frames[1];
    assert(a==b); // retry is byte-for-byte identical, including sequence and CRC
    assert(frames[2]==frames[3]);
    assert(frames[4]==frames[5]);
    assert(critical_depth==0);
    if(argc==2){
        FILE *out=std::fopen(argv[1],"w");assert(out);
        for(const auto &frame:frames)for(auto byte:frame)
            assert(std::fprintf(out,"%02x\n",byte)>0);
        assert(std::fclose(out)==0);
    }
    nack_hello=true;
    ui16_link_monitor_reset();
    assert(!ui16_link_prepare());
    const auto attempted=frames.size();
    assert(!ui16_link_prepare() && frames.size()==attempted);
    std::puts("BL616 reliable link v2: HELLO, lost/corrupt ACK, lost font packet, retry, CRC32 and incompatible fallback passed");
}
