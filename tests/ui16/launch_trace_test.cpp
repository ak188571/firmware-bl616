#include "tc_launch_trace.h"
#include <cassert>
#include <cstdio>
static std::uint64_t now=100;
std::uint64_t tc_launch_now_us(){return now;}
int main(){
    tc_launch_mark(TcLaunchEvent::CoreReady);
    assert(tc_launch_record.seen==0);
    {
        TcLaunchScope scope;
        now+=500000;tc_launch_mark(TcLaunchEvent::NoticeDone);
        now+=10;tc_launch_mark(TcLaunchEvent::NoticeDone);
        tc_launch_block(1024,20,5100);
        tc_launch_block(16,5,80);
        tc_launch_success();
    }
    assert(!tc_launch_record.active&&tc_launch_record.success);
    assert(tc_launch_record.elapsed_us[static_cast<unsigned>(TcLaunchEvent::NoticeDone)]==500000);
    assert(tc_launch_record.elapsed_us[static_cast<unsigned>(TcLaunchEvent::Finished)]==500010);
    assert(tc_launch_record.rom_bytes==1040&&tc_launch_record.blocks==2);
    assert(tc_launch_record.sd_read_us==25&&tc_launch_record.uart_enqueue_us==5180);
    const auto seen=tc_launch_record.seen;
    now+=900;tc_launch_mark(TcLaunchEvent::CoreReady);tc_launch_block(10,1,1);
    assert(tc_launch_record.seen==seen&&tc_launch_record.rom_bytes==1040);
    {TcLaunchScope failure;now+=200;}
    assert(!tc_launch_record.active&&!tc_launch_record.success&&tc_launch_record.blocks==0);
    assert(tc_launch_record.elapsed_us[static_cast<unsigned>(TcLaunchEvent::Finished)]==200);
    std::puts("Launch trace: timestamps, aggregates, freeze, reset and failed scope passed");
}
