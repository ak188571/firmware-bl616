#pragma once
#include <cstdint>
#include "tc_rom_measure.h"

// RAM-only timing. No UART traffic, allocation, filesystem I/O, or extra waits.
enum class TcLaunchEvent : unsigned {
    Accepted, PreflightDone, NoticeDone, ProgramBegin, ProgramEnd, CoreReady,
    OsdReady, IdentityBegin, IdentityEnd, SettingsEnd, RomBegin, RomQueued,
    ReleaseQueued, LoadReturned, Finished, Count
};
struct TcLaunchRecord {
    std::uint64_t origin_us=0;
    std::uint64_t elapsed_us[static_cast<unsigned>(TcLaunchEvent::Count)]{};
    std::uint64_t sd_read_us=0, uart_enqueue_us=0;
    std::uint32_t seen=0, rom_bytes=0, blocks=0;
    std::uint32_t rom_baud=2000000, requests=0, retries=0, stream_crc=0;
    std::uint32_t rom_window=1;
    std::uint32_t rom_pipeline=0,cpu_pumps=0,cpu_prepared=0,cpu_acks=0,cpu_crc=0;
    std::uint64_t end_ack_us=0;
    TcRomMeasureRecord profile[static_cast<unsigned>(TcRomMeasurePhase::Count)]{};
    std::uint64_t profile_clock_calibration_us=0;
    std::uint32_t profile_clock_reads=0,profile_error=0;
    bool profile_enabled=false;
    bool active=false, success=false;
};
#ifdef TC_LAUNCH_TRACE
extern TcLaunchRecord tc_launch_record;
std::uint64_t tc_launch_now_us();
void tc_launch_begin();
void tc_launch_mark(TcLaunchEvent event);
void tc_launch_success();
void tc_launch_finish();
void tc_launch_block(std::uint32_t bytes,std::uint64_t read_us,std::uint64_t enqueue_us);
inline std::uint64_t tc_launch_clock(){return tc_launch_now_us();}
#else
inline void tc_launch_begin(){}
inline void tc_launch_mark(TcLaunchEvent){}
inline void tc_launch_success(){}
inline void tc_launch_finish(){}
inline void tc_launch_block(std::uint32_t,std::uint64_t,std::uint64_t){}
inline std::uint64_t tc_launch_clock(){return 0;}
#endif
struct TcLaunchScope {
    TcLaunchScope(){tc_launch_begin();}
    ~TcLaunchScope(){tc_launch_finish();}
    TcLaunchScope(const TcLaunchScope &)=delete;
    TcLaunchScope &operator=(const TcLaunchScope &)=delete;
};
