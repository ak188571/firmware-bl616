#include "tc_launch_export.h"
#ifdef TC_LAUNCH_TRACE
#include "tc_launch_trace.h"
#include "tc_transition_trace.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace {
const char *const names[]={"Accepted","PreflightDone","NoticeDone","ProgramBegin","ProgramEnd",
    "CoreReady","OsdReady","IdentityBegin","IdentityEnd","SettingsEnd","RomBegin","RomQueued",
    "ReleaseQueued","LoadReturned","Finished","DataBegin","DataEnd","EndBegin","EndEnd"};
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
    if(tc_transition_records[0].started && !tc_transition_records[1].finished)return false;
    if(!rom||std::strlen(rom)>511||core_id!=2||!buffer||capacity<2048||
       !output_path||output_capacity<32||tc_launch_record.active||
       !(tc_launch_record.seen&(1u<<static_cast<unsigned>(TcLaunchEvent::Finished))))return false;
    const auto record=tc_launch_record;
    Text text{static_cast<char*>(buffer),capacity};
    const char *format=record.rom_baud==2000000?"TC_UART_BASELINE_V1":
        record.rom_pipeline?(record.profile_enabled?"TC_ROMXFR1_PROFILE_V4":"TC_ROMXFR1_TRACE_V4"):
        record.profile_enabled?"TC_ROMXFR1_PROFILE_V3":"TC_ROMXFR1_TRACE_V2";
    text.add("%s\ncore=%d\nbaud=%lu\nrom_hex=",format,
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
    if(record.rom_baud!=2000000)text.add("window=%lu\n",static_cast<unsigned long>(record.rom_window));
    if(record.rom_pipeline)text.add("cpu_pipeline=1\ncpu_pumps=%lu\ncpu_prepared=%lu\ncpu_acks=%lu\ncpu_crc=%lu\n",
        static_cast<unsigned long>(record.cpu_pumps),static_cast<unsigned long>(record.cpu_prepared),
        static_cast<unsigned long>(record.cpu_acks),static_cast<unsigned long>(record.cpu_crc));
    for(unsigned i=0;i<static_cast<unsigned>(TcLaunchEvent::Count);++i)
        if(record.seen&(1u<<i))text.add("%s_us=%llu\n",names[i],static_cast<unsigned long long>(record.elapsed_us[i]));
    if(record.profile_enabled){
        text.add("profile_error=%lu\nprofile_clock_reads=%lu\nclock_probe_1000_us=%llu\n",
            static_cast<unsigned long>(record.profile_error),static_cast<unsigned long>(record.profile_clock_reads),
            static_cast<unsigned long long>(record.profile_clock_calibration_us));
        const char *const activities[]={"cpu","copy","frame_crc","stream_crc","reply_crc","sd","tx_setup","tx_wait","tx_drain","ack_cpu","ack_wait"};
        static_assert(sizeof activities/sizeof activities[0]==TcRom::ActivityCount,"Activity names");
        for(unsigned phase=0;phase<2;++phase){
            const char prefix=phase==0?'D':'E';const auto &r=record.profile[phase];
            text.add("%c_wall_us=%llu\n",prefix,static_cast<unsigned long long>(r.elapsed_us));
            for(unsigned i=0;i<TcRom::ActivityCount;++i)text.add("%c_%s_us=%llu\n",prefix,activities[i],static_cast<unsigned long long>(r.us[i]));
            for(unsigned i=2;i<=5;++i)text.add("%c_%s_calls=%lu\n%c_%s_bytes=%lu\n",prefix,activities[i],static_cast<unsigned long>(r.calls[i]),prefix,activities[i],static_cast<unsigned long>(r.units[i]));
            text.add("%c_sd_max_us=%llu\n%c_ack_wait_max_us=%llu\n",prefix,static_cast<unsigned long long>(r.max_us[5]),prefix,static_cast<unsigned long long>(r.max_us[10]));
            text.add("%c_tx_frames=%lu\n%c_tx_bytes=%lu\n%c_tx_failures=%lu\n",prefix,static_cast<unsigned long>(r.tx_frames),prefix,static_cast<unsigned long>(r.tx_bytes),prefix,static_cast<unsigned long>(r.tx_failures));
            text.add("%c_tx_envelope_us=%llu\n%c_tx_gap_us=%llu\n%c_tx_gap_max_us=%llu\n%c_tx_span_us=%llu\n%c_sd_in_send_us=%llu\n",
                prefix,static_cast<unsigned long long>(r.tx_envelope_us),prefix,static_cast<unsigned long long>(r.tx_gap_us),
                prefix,static_cast<unsigned long long>(r.tx_gap_max_us),prefix,static_cast<unsigned long long>(r.tx_span_us),prefix,static_cast<unsigned long long>(r.sd_in_send_us));
            text.add("%c_ack_calls=%lu\n%c_ack_ready=%lu\n%c_ack_empty_polls=%lu\n",prefix,static_cast<unsigned long>(r.ack_calls),prefix,static_cast<unsigned long>(r.ack_ready),prefix,static_cast<unsigned long>(r.ack_empty_polls));
        }
    }
    if(record.spi_hz)text.add("physical_transport=SPI_MODE1_DMA\nspi_hz=%lu\n",static_cast<unsigned long>(record.spi_hz));
#ifdef TC_JTAG_SPI_HZ
    text.add("jtag_transport=SPI0_MODE3_DMA_GPIO_TAIL\njtag_hz=%lu\n",
        static_cast<unsigned long>(tc_jtag_spi_hz));
#endif
    const char *const steps[]={"program","reload","boot_wait","snapshot","file_prepare",
        "erase_first","erase_second","write_begin","stream","sd_read","jtag_shift","write_end",
        "wait_core","rom_identity","settings","input_release","font_reset","redraw"};
    static_assert(sizeof(steps)/sizeof(steps[0])==static_cast<unsigned>(TcTransitionStep::Count),"Section names");
    for(unsigned phase=0;phase<2;++phase){
        const auto &transition=tc_transition_records[phase];
        const char prefix=phase==0?'L':'R';
#ifdef TC_JTAG_SPI_HZ
        const auto &jtag=transition.jtag_spi;
        text.add("%c_jtag_attempts=%u\n%c_jtag_first_error=%u\n%c_jtag_error=%u\n",prefix,
            transition.jtag_spi_attempts,prefix,transition.jtag_spi_first_error,prefix,unsigned(jtag.error));
        text.add("%c_jtag_dma_bytes=%lu\n%c_jtag_dma_blocks=%lu\n%c_jtag_gpio_bytes=%lu\n",prefix,
            static_cast<unsigned long>(jtag.dma_bytes),prefix,static_cast<unsigned long>(jtag.dma_blocks),
            prefix,static_cast<unsigned long>(jtag.gpio_bytes));
        text.add("%c_jtag_config=%08lx\n%c_jtag_period=%08lx\n",prefix,
            static_cast<unsigned long>(jtag.config),prefix,static_cast<unsigned long>(jtag.period));
#endif
        const auto &boot=transition.boot;
        const auto &attempt=transition.short_boot;
        if (attempt.attempted) {
            text.add("%c_short_boot_attempt=1\n%c_short_boot_reload=%u\n%c_short_boot_accepted=%u\n%c_short_boot_program=%u\n%c_short_boot_configured=%u\n",
                     prefix,prefix,unsigned(attempt.reload_started),prefix,unsigned(attempt.wait_accepted),
                     prefix,unsigned(attempt.program_entered),prefix,unsigned(attempt.configured));
            text.add("%c_short_boot_gpio_before=%08lx\n%c_short_boot_gpio_after=%08lx\n%c_short_boot_gpio_mask=%08lx\n%c_short_boot_gpio_expected=%08lx\n",
                     prefix,static_cast<unsigned long>(attempt.gpio_before_select),
                     prefix,static_cast<unsigned long>(attempt.gpio_after_select),
                     prefix,static_cast<unsigned long>(attempt.gpio_mask),
                     prefix,static_cast<unsigned long>(attempt.gpio_expected));
            text.add("%c_short_boot_end_us=%lu\n%c_short_boot_probes=%u\n",prefix,
                     static_cast<unsigned long>(attempt.wait_end_us),prefix,attempt.probe_count);
            text.add("%c_short_boot_fields=us_after_idle,gpio28_hex,checks_hex\n",prefix);
            text.add("%c_short_boot_bits=snapshot,id,user,status,fifo_empty,bus_idle,logo_no_timeout,native,min_wait,within_deadline,stable,logo_attempted\n",prefix);
            for (unsigned i=0;i<attempt.probe_count;++i) {
                const auto &probe=attempt.probes[i];
                text.add("%c_short_boot_%02u=%lu,%08lx,%03x\n",prefix,i+1,
                         static_cast<unsigned long>(probe.elapsed_us),static_cast<unsigned long>(probe.gpio28),
                         unsigned(probe.checks));
            }
        }
        if (boot.samples) {
            text.add("%c_boot_probe_origin=reload_start\n%c_boot_samples=%u\n%c_boot_match_mask=%llx\n",
                     prefix,prefix,boot.samples,prefix,static_cast<unsigned long long>(boot.matches));
            text.add("%c_boot_idle_end_us=%llu\n%c_boot_first_match_us=%llu\n%c_boot_third_match_us=%llu\n%c_boot_last_miss_us=%llu\n",
                     prefix,static_cast<unsigned long long>(boot.idle_end_us),
                     prefix,static_cast<unsigned long long>(boot.first_match_us),
                     prefix,static_cast<unsigned long long>(boot.third_match_us),
                     prefix,static_cast<unsigned long long>(boot.last_miss_us));
            text.add("%c_boot_uart_idle_us=%llu\n%c_boot_uart_timeout=%u\n%c_boot_loop_end_us=%llu\n%c_boot_preerase_us=%llu\n",
                     prefix,static_cast<unsigned long long>(boot.uart_idle_us),prefix,unsigned(boot.uart_timeout),
                     prefix,static_cast<unsigned long long>(boot.end_us),prefix,static_cast<unsigned long long>(boot.preerase_us));
            text.add("%c_boot_preerase_id=%lx\n%c_boot_preerase_user=%lx\n%c_boot_preerase_status=%lx\n",
                     prefix,static_cast<unsigned long>(boot.pre_id),prefix,static_cast<unsigned long>(boot.pre_user),
                     prefix,static_cast<unsigned long>(boot.pre_status));
        }
        text.add("%c_started=%u\n%c_finished=%u\n%c_wall_us=%llu\n",prefix,unsigned(transition.started),
            prefix,unsigned(transition.finished),prefix,static_cast<unsigned long long>(transition.wall_us));
        for(unsigned i=0;i<static_cast<unsigned>(TcTransitionStep::Count);++i)
            text.add("%c_%s_us=%llu\n%c_%s_calls=%lu\n",prefix,steps[i],
                static_cast<unsigned long long>(transition.section_us[i]),prefix,steps[i],
                static_cast<unsigned long>(transition.calls[i]));
    }
    text.add("section_accounting=inclusive_nested\nreturn_boundary=accepted_return_to_list_to_list_redraw_complete\n");
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
