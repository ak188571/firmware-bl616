#include "tc_rom_protocol.h"
#include <array>
#include <cstring>
namespace TcRom {
namespace {
constexpr std::array<std::uint32_t,256> crc_table(){
    std::array<std::uint32_t,256> result{};
    for(unsigned i=0;i<result.size();++i){
        auto c=static_cast<std::uint32_t>(i);
        for(unsigned bit=0;bit<8;++bit)c=(c>>1)^((c&1)?0xedb88320u:0);
        result[i]=c;
    }
    return result;
}
constexpr auto CrcTable=crc_table(); // 1 KiB read-only; no startup allocation.
}
std::uint32_t crc_update(std::uint32_t state,const void *data,std::size_t size){
    const auto *bytes=static_cast<const std::uint8_t*>(data);
    for(std::size_t i=0;i<size;++i)state=CrcTable[(state^bytes[i])&0xff]^(state>>8);
    return state;
}
std::uint32_t get32(const std::uint8_t *p){return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];}
void put32(std::uint8_t *p,std::uint32_t v){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(v>>(24-8*i));}
std::size_t encode(std::uint8_t *out,std::size_t capacity,const View &f){
    if(!out||!f.session||f.count>MaxData||capacity<Overhead+f.count||(!f.data&&f.count)||
       (f.command!=Request&&f.command!=Reply))return 0;
    // Source may already occupy out+20; move it before writing the header.
    if(f.count)std::memmove(out+20,f.data,f.count);
    const auto length=static_cast<std::uint16_t>(21+f.count);
    out[0]=0xaa;out[1]=static_cast<std::uint8_t>(length>>8);out[2]=static_cast<std::uint8_t>(length);
    out[3]=f.command;out[4]=Version;out[5]=static_cast<std::uint8_t>(f.op);
    put32(out+6,f.session);put32(out+10,f.sequence);put32(out+14,f.offset);
    out[18]=static_cast<std::uint8_t>(f.count>>8);out[19]=static_cast<std::uint8_t>(f.count);
    put32(out+20+f.count,crc_update(0xffffffffu,out,20+f.count)^0xffffffffu);
    return Overhead+f.count;
}
bool decode(const std::uint8_t *p,std::size_t size,View &f){
    if(!p||size<Overhead||size>MaxFrame||p[0]!=0xaa||p[4]!=Version||
       (p[3]!=Request&&p[3]!=Reply))return false;
    const auto count=static_cast<std::uint16_t>((unsigned(p[18])<<8)|p[19]);
    if(size!=Overhead+count||count>MaxData||((unsigned(p[1])<<8)|p[2])!=size-3||!get32(p+6)||
       get32(p+size-4)!=(crc_update(0xffffffffu,p,size-4)^0xffffffffu))return false;
    f={p[3],static_cast<Op>(p[5]),get32(p+6),get32(p+10),get32(p+14),p+20,count};return true;
}
bool Session::request(Op op,const std::uint8_t *data,std::uint16_t count,std::uint32_t timeout,
                      void (*pump)(void *),void *context,bool any_offset){
    last_status_=Status::Busy;
    if(!transport_.exchange||!id_)return false;
    const auto seq=op==Op::Abort?0:sequence_,at=op==Op::Abort?0:offset_;
    const auto size=encode(storage_,capacity_,{Request,op,id_,seq,at,data,count});
    if(!size)return false;
    const auto expected=offset_+(op==Op::Data?count:0);
    for(unsigned attempt=0;attempt<4;++attempt){
        ++stats_.requests;if(attempt)++stats_.retries;
        std::uint8_t reply[Overhead+1]{};std::size_t reply_size=sizeof reply;
        if(!transport_.exchange(transport_.context,storage_,size,reply,&reply_size,timeout,pump,context))continue;
        View answer;
        if(!decode(reply,reply_size,answer)||answer.command!=Reply||answer.op!=op||answer.session!=id_||
           answer.sequence!=seq||answer.count!=1)continue;
        const auto status=static_cast<Status>(answer.data[0]);last_status_=status;
        if(status==Status::Ok&&(any_offset||answer.offset==expected))return true;
        if(status!=Status::Busy&&status!=Status::Ok)return false;
    }
    return false;
}
bool Session::begin(std::uint32_t total){
    if(begun_||!total)return false;
    total_=total;std::uint8_t body[4];put32(body,total);
    begun_=request(Op::Begin,body,4);return begun_;
}
bool Session::stream(Source source){
    if(!begun_||verified_||!source.read||capacity_<MaxFrame)return false;
    if(window_two_)return stream_window(source);
    while(offset_<total_){
        std::uint16_t n=0;
        const auto wanted=static_cast<std::uint16_t>(total_-offset_<MaxData?total_-offset_:MaxData);
        if(!source.read(source.context,offset_,storage_+20,wanted,&n)||!n||n>wanted)return false;
        if(!request(Op::Data,storage_+20,n,50,source.pump,source.context))return false;
        crc_=crc_update(crc_,storage_+20,n);offset_+=n;++sequence_;++stats_.blocks;stats_.bytes+=n;
    }
    stats_.crc=crc_^0xffffffffu;return true;
}
bool Session::finish(){
    if(!begun_||offset_!=total_||verified_)return false;
    std::uint8_t body[4];put32(body,crc_^0xffffffffu);
    verified_=request(Op::End,body,4,10000);return verified_;
}
bool Session::start(){
    if(!verified_||running_)return false;
    running_=request(Op::Start,nullptr,0);return running_;
}
bool Session::link_rate(std::uint32_t baud){
    if(!begun_||running_)return false;
    std::uint8_t body[4];put32(body,baud);return request(Op::LinkRate,body,4);
}
bool Session::link_sync(){return begun_&&!running_&&request(Op::LinkSync,nullptr,0);}
void Session::abort(){if(begun_&&!running_)request(Op::Abort,nullptr,0,50,nullptr,nullptr,true);}
}

namespace TcRom {
bool Session::negotiate_window(){
    if(!begun_||offset_||verified_)return false;
    if(!transport_.send||!transport_.receive||capacity_<2*MaxFrame)return true;
    std::uint8_t body[4];put32(body,2);
    window_two_=request(Op::Window,body,4);
    stats_.window=window_two_?2:1;
    // An old ROMXFR1 core explicitly rejects the extension. Silence, a backend
    // failure or any other error must not be mistaken for old-core support.
    return window_two_||last_status_==Status::Format;
}
bool Session::stream_window(Source source){
    struct Slot {std::uint32_t seq=0,at=0;std::uint16_t n=0;unsigned attempts=0;bool ack=false;};
    Slot slots[2];unsigned head=0,used=0;
    auto queued=offset_;auto seq=sequence_;
    auto send=[&](unsigned i){
        auto &s=slots[i];if(s.attempts==4)return false;
        ++stats_.requests;if(s.attempts++)++stats_.retries;
        // Failure can leave a partial frame. Retry after the receive timeout;
        // framing/CRC and the receiver's inter-byte timeout reject the prefix.
        return transport_.send(transport_.context,storage_+i*MaxFrame,Overhead+s.n,50,source.pump,source.context);
    };
    bool recovery=false;
    while(offset_<total_){
        while(used<2&&queued<total_&&!recovery){
            const auto i=(head+used)%2;auto &s=slots[i];s={};s.seq=seq;s.at=queued;
            const auto wanted=static_cast<std::uint16_t>(total_-queued<MaxData?total_-queued:MaxData);
            auto *frame=storage_+i*MaxFrame;
            if(!source.read(source.context,queued,frame+20,wanted,&s.n)||!s.n||s.n>wanted)return false;
            if(!encode(frame,MaxFrame,{Request,Op::Data,id_,seq,queued,frame+20,s.n}))return false;
            queued+=s.n;++seq;++used;
            if(!send(i)){recovery=true;break;}
        }
        std::uint8_t reply[Overhead+1];std::size_t size=sizeof reply;View answer;
        bool retry=!transport_.receive(transport_.context,reply,&size,50);
        if(!retry&&decode(reply,size,answer)&&answer.command==Reply&&answer.op==Op::Data&&answer.session==id_&&answer.count==1){
            for(unsigned k=0;k<used;++k){
                auto &s=slots[(head+k)%2];if(answer.sequence!=s.seq)continue;
                const auto status=static_cast<Status>(answer.data[0]);
                if(status==Status::Ok&&answer.offset==s.at+s.n)s.ack=true;
                // Drain replies already in flight before going back. Retrying
                // immediately on ORDER could multiply queued copies when an
                // earlier corrupted DATA is still missing.
                else if(status==Status::Busy||status==Status::Order)recovery=true;
                else if(status!=Status::Ok)return false;
            }
        }else if(!retry)retry=true;
        // Only contiguous ACKs release window credits. An ACK for the second
        // slot cannot evict the first from the receiver's two-entry history.
        while(used&&slots[head].ack){
            const auto &s=slots[head];crc_=crc_update(crc_,storage_+head*MaxFrame+20,s.n);
            offset_+=s.n;++sequence_;++stats_.blocks;stats_.bytes+=s.n;
            head^=1;--used;
        }
        if(retry){
            recovery=false;
            // Go back over unacknowledged slots in order. Exact stored bytes,
            // session, sequence and offset survive every attempt.
            for(unsigned k=0;k<used;++k){const auto i=(head+k)%2;
                if(!slots[i].ack){if(slots[i].attempts==4)return false;if(!send(i))recovery=true;}
            }
        }
    }
    stats_.crc=crc_^0xffffffffu;return true;
}
}
