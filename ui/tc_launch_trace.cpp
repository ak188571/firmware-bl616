#include "tc_launch_trace.h"
#ifdef TC_LAUNCH_TRACE
TcLaunchRecord tc_launch_record;
void tc_launch_begin(){
    tc_launch_record={};
    tc_launch_record.origin_us=tc_launch_now_us();
    tc_launch_record.active=true;
    tc_launch_record.seen=1u<<static_cast<unsigned>(TcLaunchEvent::Accepted);
}
void tc_launch_mark(TcLaunchEvent event){
    const auto index=static_cast<unsigned>(event);
    if(!tc_launch_record.active || index>=static_cast<unsigned>(TcLaunchEvent::Count))return;
    const auto bit=1u<<index;
    if(tc_launch_record.seen&bit)return;
    tc_launch_record.elapsed_us[index]=tc_launch_now_us()-tc_launch_record.origin_us;
    tc_launch_record.seen|=bit;
}
void tc_launch_success(){if(tc_launch_record.active)tc_launch_record.success=true;}
void tc_launch_finish(){tc_launch_mark(TcLaunchEvent::Finished);tc_launch_record.active=false;}
void tc_launch_block(std::uint32_t bytes,std::uint64_t read_us,std::uint64_t enqueue_us){
    if(!tc_launch_record.active)return;
    tc_launch_record.rom_bytes+=bytes;
    ++tc_launch_record.blocks;
    tc_launch_record.sd_read_us+=read_us;
    tc_launch_record.uart_enqueue_us+=enqueue_us;
}
#endif
