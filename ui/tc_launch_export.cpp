#include "tc_launch_export.h"
#ifdef TC_LAUNCH_TRACE
#include "tc_launch_trace.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace {
const char *const names[]={"Accepted","PreflightDone","NoticeDone","ProgramBegin","ProgramEnd",
    "CoreReady","OsdReady","IdentityBegin","IdentityEnd","SettingsEnd","RomBegin","RomQueued",
    "ReleaseQueued","LoadReturned","Finished"};
static_assert(sizeof(names)/sizeof(names[0])==static_cast<unsigned>(TcLaunchEvent::Count),"Event names");
struct Text {
    char *buffer;UINT capacity,used=0;bool good=true;
    void add(const char *format,...){
        if(!good)return;
        va_list args;va_start(args,format);
        const int n=std::vsnprintf(buffer+used,capacity-used,format,args);
        va_end(args);
        if(n<0||static_cast<unsigned>(n)>=capacity-used){good=false;return;}
        used+=static_cast<UINT>(n);
    }
};
}
bool tc_launch_export(const char *rom,int core_id,FIL &file,void *buffer,UINT capacity,
                      char *output_path,UINT output_capacity){
    if(output_path&&output_capacity)output_path[0]=0;
    if(!rom||std::strlen(rom)>511||core_id!=2||!buffer||capacity<2048||
       !output_path||output_capacity<32||tc_launch_record.active||
       !(tc_launch_record.seen&(1u<<static_cast<unsigned>(TcLaunchEvent::Finished))))return false;
    const auto record=tc_launch_record;
    Text text{static_cast<char*>(buffer),capacity};
    text.add("%s\ncore=%d\nbaud=%lu\nrom_hex=",record.rom_baud==2000000?"TC_UART_BASELINE_V1":"TC_ROMXFR1_TRACE_V2",
             core_id,static_cast<unsigned long>(record.rom_baud));
    for(const unsigned char *p=reinterpret_cast<const unsigned char*>(rom);*p;++p)text.add("%02x",unsigned(*p));
    text.add("\norigin_us=%llu\nsuccess=%u\nseen=%lu\nrom_bytes=%lu\nblocks=%lu\nsd_read_us=%llu\nuart_enqueue_us=%llu\n",
        static_cast<unsigned long long>(record.origin_us),unsigned(record.success),
        static_cast<unsigned long>(record.seen),static_cast<unsigned long>(record.rom_bytes),
        static_cast<unsigned long>(record.blocks),static_cast<unsigned long long>(record.sd_read_us),
        static_cast<unsigned long long>(record.uart_enqueue_us));
    if(record.rom_baud!=2000000)text.add("transport=ROMXFR1\nstream_boundary=DATA_ACK\nrequests=%lu\nretries=%lu\nstream_crc=%08lx\nEndAck_us=%llu\n",
        static_cast<unsigned long>(record.requests),static_cast<unsigned long>(record.retries),
        static_cast<unsigned long>(record.stream_crc),static_cast<unsigned long long>(record.end_ack_us));
    for(unsigned i=0;i<static_cast<unsigned>(TcLaunchEvent::Count);++i)
        if(record.seen&(1u<<i))text.add("%s_us=%llu\n",names[i],static_cast<unsigned long long>(record.elapsed_us[i]));
    text.add("END=1\n");
    if(!text.good)return false;
    bool opened=false;
    for(unsigned index=1;index<=9999;++index){
        std::snprintf(output_path,output_capacity,"sd:/uart-%04u.txt",index);
        const auto result=f_open(&file,output_path,FA_WRITE|FA_CREATE_NEW);
        if(result==FR_EXIST)continue;
        if(result!=FR_OK)return false;
        opened=true;break;
    }
    if(!opened)return false;
    UINT written=0;
    bool good=f_write(&file,buffer,text.used,&written)==FR_OK&&written==text.used;
    if(good&&f_sync(&file)!=FR_OK)good=false;
    if(f_close(&file)!=FR_OK)good=false;
    return good;
}
#endif
