#include "tc_rom_profile.h"
#include "tc_rom_protocol.h"
#include "tc_rom_measure.h"
#include "tc_launch_trace.h"
#include <array>
#include <cassert>
#include <cstdio>
static std::uint64_t now=0;
std::uint64_t tc_launch_now_us(){return now;}
static unsigned idx(TcRom::Activity a){return static_cast<unsigned>(a);}
int main(){
    using namespace TcRom;
    tc_launch_begin();tc_rom_measure_prepare();tc_rom_measure_begin(TcRomMeasurePhase::Data);
    auto *p=tc_rom_measure_observer();now+=10;
    {ProfileScope copy(p,Activity::SourceCopy);now+=5;
        {ProfileScope sd(p,Activity::SdRead,4096);now+=30;}
        now+=5;
    }
    now+=10;
    tc_rom_measure_tx_begin(1048);
    {ProfileScope pump(p,Activity::Cpu);{ProfileScope sd(p,Activity::SdRead,4096);now+=20;}}
    {ProfileScope wait(p,Activity::TxWait);now+=100;}
    tc_rom_measure_tx_end(true);now+=10;
    {ProfileScope ack(p,Activity::AckProcess);
        ProfileScope wait(p,Activity::AckWait,0,false);wait.start();now+=8;wait.stop();
        {ProfileScope crc(p,Activity::ReplyCrc,21);now+=2;}
        now+=3;
    }
    tc_rom_measure_tx_begin(88);now+=15;tc_rom_measure_tx_end(true);
    tc_rom_measure_end();const auto &r=tc_launch_record.profile[0];
    assert(!tc_launch_record.profile_error);
    assert(r.elapsed_us==218&&r.us[idx(Activity::Cpu)]==45);
    assert(r.us[idx(Activity::SourceCopy)]==10&&r.us[idx(Activity::SdRead)]==50);
    assert(r.us[idx(Activity::TxWait)]==100&&r.us[idx(Activity::AckWait)]==8);
    assert(r.us[idx(Activity::AckProcess)]==3&&r.us[idx(Activity::ReplyCrc)]==2);
    assert(r.tx_envelope_us==135&&r.tx_gap_us==23&&r.tx_span_us==158&&r.sd_in_send_us==20);
    assert(r.calls[idx(Activity::SdRead)]==2&&r.units[idx(Activity::SdRead)]==8192);
    tc_rom_measure_begin(TcRomMeasurePhase::End);
    std::array<std::uint8_t,MaxFrame> a{},b{};const std::uint8_t data[]={1,2,3};
    const View frame{Request,Op::Data,1,2,3,data,3};
    const auto n=encode(a.data(),a.size(),frame,p);assert(n==encode(b.data(),b.size(),frame));assert(a==b);
    View decoded;assert(decode(a.data(),n,decoded,p));a[n-1]^=1;assert(!decode(a.data(),n,decoded,p));
    tc_rom_measure_end();const auto &e=tc_launch_record.profile[1];
    assert(e.calls[idx(Activity::FrameCrc)]==1&&e.units[idx(Activity::FrameCrc)]==23);
    assert(e.calls[idx(Activity::ReplyCrc)]==2&&e.units[idx(Activity::ReplyCrc)]==46);
    assert(!tc_launch_record.profile_error);
    std::puts("Exclusive wall partition, nested SD overlap, TX envelopes/gaps, CRC observation and unchanged/corrupt frames passed");
}
