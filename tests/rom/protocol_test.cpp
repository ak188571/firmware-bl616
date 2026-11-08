#include "tc_rom_protocol.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>
using namespace TcRom;
struct Peer {
    std::vector<std::uint8_t> bytes,last;
    std::uint32_t id=0,at=0,seq=0,crc=0xffffffffu;
    unsigned writes=0,calls=0;bool verified=false,running=false,aborted=false;
    bool lose_once=false,corrupt_data=false,corrupt_reply=false,all_lost=false;
    std::set<std::pair<unsigned,std::uint32_t>> seen;
    static bool exchange(void *p,const std::uint8_t *wire,std::size_t n,std::uint8_t *reply,std::size_t *reply_n,
                         std::uint32_t,void (*pump)(void *),void *pump_context){
        auto &s=*static_cast<Peer*>(p);++s.calls;if(pump)pump(pump_context);
        std::vector<std::uint8_t> incoming(wire,wire+n);View f;
        if(s.corrupt_data&&wire[5]==2){incoming[20]^=1;s.corrupt_data=false;}
        if(!decode(incoming.data(),incoming.size(),f))return false;
        Status status=Status::Ok;
        if(f.op==Op::Begin){
            if(f.count!=4||f.sequence||f.offset)status=Status::Format;
            else if(s.id==f.session){if(s.bytes.size()!=get32(f.data))status=Status::Session;}
            else {s.id=f.session;s.bytes.resize(get32(f.data));s.at=s.seq=0;s.crc=0xffffffffu;}
        }else if(f.session!=s.id)status=Status::Session;
        else if(f.op==Op::Data){
            if(s.last==incoming){}
            else if(f.sequence!=s.seq||f.offset!=s.at)status=Status::Order;
            else if(s.at+f.count>s.bytes.size())status=Status::Range;
            else {std::copy_n(f.data,f.count,s.bytes.begin()+s.at);s.at+=f.count;++s.seq;++s.writes;s.crc=crc_update(s.crc,f.data,f.count);s.last=incoming;}
        }else if(f.op==Op::End){s.verified=f.count==4&&s.at==s.bytes.size()&&get32(f.data)==(s.crc^0xffffffffu);if(!s.verified)status=Status::Integrity;}
        else if(f.op==Op::Start){if(!s.verified)status=Status::State;else s.running=true;}
        else if(f.op==Op::Abort){s.aborted=true;s.running=false;}
        else if(f.op!=Op::LinkRate&&f.op!=Op::LinkSync)status=Status::Format;
        const auto tag=std::make_pair(static_cast<unsigned>(f.op),f.sequence);
        const auto code=static_cast<std::uint8_t>(status);
        const auto size=encode(reply,*reply_n,{Reply,f.op,f.session,f.sequence,s.at,&code,1});
        if(!size)return false;
        *reply_n=size;
        if(s.all_lost)return false;
        if(s.lose_once&&s.seen.insert(tag).second)return false;
        if(s.corrupt_reply){reply[size-1]^=1;s.corrupt_reply=false;}
        return true;
    }
};
struct Input {
    std::vector<std::uint8_t> data;unsigned pumps=0;bool fail=false;
    static bool read(void *p,std::uint32_t at,std::uint8_t *out,std::uint16_t max,std::uint16_t *n){
        auto &s=*static_cast<Input*>(p);if(s.fail)return false;
        *n=static_cast<std::uint16_t>(std::min<std::size_t>(max,s.data.size()-at));std::copy_n(s.data.data()+at,*n,out);return true;
    }
    static void pump(void *p){++static_cast<Input*>(p)->pumps;}
};
int main(){
    assert((crc_update(0xffffffffu,"123456789",9)^0xffffffffu)==0xcbf43926u);
    const std::array<std::uint8_t,27> golden={0xaa,0,0x18,0x30,1,2,0x12,0x34,0x56,0x78,0,0,0,2,0,0,4,0,0,3,'a','b','c',0x6b,0x9f,0xdf,0x61};
    std::array<std::uint8_t,MaxFrame> buffer{};const std::uint8_t data[]={'a','b','c'};
    assert(encode(buffer.data(),buffer.size(),{Request,Op::Data,0x12345678,2,1024,data,3})==golden.size());
    assert(std::equal(golden.begin(),golden.end(),buffer.begin()));
    for(std::size_t bit=0;bit<golden.size()*8;++bit){auto bad=golden;bad[bit/8]^=1u<<(bit%8);View v;assert(!decode(bad.data(),bad.size(),v));}
    for(unsigned size:{1u,63u,64u,65u,1024u,1025u,2u*1024*1024,4u*1024*1024}){
        for(unsigned fault=0;fault<4;++fault){
            Peer peer;peer.lose_once=fault==1;peer.corrupt_data=fault==2;peer.corrupt_reply=fault==3;
            Input input;input.data.resize(size);for(unsigned i=0;i<size;++i)input.data[i]=static_cast<std::uint8_t>(i*37+11);
            Session session({&peer,Peer::exchange},7,buffer.data(),buffer.size());
            assert(session.begin(size));
            // Optional link controls are used by UART only. The other runs use
            // the same Session with no physical-transport-specific operation.
            if(fault==0){assert(session.link_rate(8000000));assert(session.link_sync());}
            assert(session.stream({&input,Input::read,Input::pump}));assert(session.finish());
            if(fault==0){assert(session.link_rate(2000000));assert(session.link_sync());}
            assert(session.start());assert(peer.running&&peer.bytes==input.data);
            assert(peer.writes==(size+1023)/1024&&input.pumps>=peer.writes);
        }
    }
    Peer absent;absent.all_lost=true;Session no_ack({&absent,Peer::exchange},9,buffer.data(),buffer.size());
    assert(!no_ack.begin(1)&&absent.calls==4&&!absent.running);
    Peer failed;Session bad_source({&failed,Peer::exchange},10,buffer.data(),buffer.size());
    Input input;input.fail=true;assert(bad_source.begin(10));assert(!bad_source.stream({&input,Input::read,Input::pump}));
    assert(!bad_source.finish()&&!bad_source.start());bad_source.abort();assert(failed.aborted&&!failed.running);
    std::puts("Common ROM session: known wire/CRC vectors, all single-bit errors, 2/4 MiB, lost/corrupt ACK/data, exact retries, source failure and bounded retry passed");
}
