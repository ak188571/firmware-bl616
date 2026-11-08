#include "tc_rom_measure.h"
#ifdef TC_ROM_PROFILE
#include "tc_launch_trace.h"
namespace {
using TcRom::Activity;
TcRomMeasureRecord *record=nullptr;
Activity stack[8]{};
std::uint64_t entered[8]{},origin=0,last=0,tx_begin=0,tx_first=0,tx_last=0;
unsigned depth=0;
bool tx_open=false,tx_seen=false;
std::uint64_t clock(){++tc_launch_record.profile_clock_reads;return tc_launch_now_us();}
void charge(std::uint64_t now){record->us[static_cast<unsigned>(stack[depth-1])]+=now-last;last=now;}
void enter(void *,Activity a,std::size_t units){
    if(!record)return;
    if(depth==8){tc_launch_record.profile_error|=1;record=nullptr;return;}
    const auto now=clock();charge(now);stack[depth]=a;entered[depth]=now;++depth;
    ++record->calls[static_cast<unsigned>(a)];record->units[static_cast<unsigned>(a)]+=units;
}
void leave(void *,Activity a){
    if(!record)return;
    if(depth<=1||stack[depth-1]!=a){tc_launch_record.profile_error|=2;record=nullptr;return;}
    const auto now=clock();charge(now);--depth;
    const auto duration=now-entered[depth];auto &maximum=record->max_us[static_cast<unsigned>(a)];
    if(duration>maximum)maximum=duration;
    if(a==Activity::SdRead&&tx_open)record->sd_in_send_us+=duration;
}
TcRom::Profile observer{nullptr,enter,leave};
}
TcRom::Profile *tc_rom_measure_observer(){return &observer;}
void tc_rom_measure_prepare(){
    record=nullptr;depth=0;tx_open=tx_seen=false;
    tc_launch_record.profile_enabled=tc_launch_record.active;
    tc_launch_record.profile_error=0;tc_launch_record.profile_clock_reads=0;
    for(auto &r:tc_launch_record.profile)r={};
    // Clock-reading/loop calibration only. Outside DATA/END partitions; no
    // assumption that every observer transition has exactly this cost.
    const auto start=tc_launch_now_us();
    for(unsigned i=0;i<1000;++i){const volatile auto sample=tc_launch_now_us();(void)sample;}
    tc_launch_record.profile_clock_calibration_us=tc_launch_now_us()-start;
}
void tc_rom_measure_begin(TcRomMeasurePhase phase){
    if(!tc_launch_record.active||!tc_launch_record.profile_enabled)return;
    if(record){tc_launch_record.profile_error|=4;return;}
    record=&tc_launch_record.profile[static_cast<unsigned>(phase)];*record={};
    depth=1;stack[0]=Activity::Cpu;origin=last=clock();tx_open=tx_seen=false;tx_first=tx_last=0;
}
void tc_rom_measure_end(){
    if(!record)return;
    const auto now=clock();charge(now);record->elapsed_us=now-origin;
    if(depth!=1)tc_launch_record.profile_error|=4;
    if(tx_open)tc_launch_record.profile_error|=8;
    std::uint64_t sum=0;for(auto t:record->us)sum+=t;
    if(sum!=record->elapsed_us)tc_launch_record.profile_error|=16;
    if(tx_seen)record->tx_span_us=tx_last-tx_first;
    record=nullptr;depth=0;tx_open=false;
}
void tc_rom_measure_tx_begin(std::size_t bytes){
    if(!record)return;
    const auto now=clock();
    if(tx_seen){const auto gap=now-tx_last;record->tx_gap_us+=gap;if(gap>record->tx_gap_max_us)record->tx_gap_max_us=gap;}
    else tx_first=now;
    tx_begin=now;tx_seen=tx_open=true;++record->tx_frames;record->tx_bytes+=bytes;
}
void tc_rom_measure_tx_end(bool success){
    if(!record||!tx_open)return;
    tx_last=clock();record->tx_envelope_us+=tx_last-tx_begin;tx_open=false;
    if(!success)++record->tx_failures;
}
void tc_rom_measure_ack_begin(){if(record)++record->ack_calls;}
void tc_rom_measure_ack_poll(int got,bool first){
    if(!record)return;
    if(got==0)++record->ack_empty_polls;
    if(first&&got>0)++record->ack_ready;
}
#endif
