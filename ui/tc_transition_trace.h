#pragma once
#include <cstdint>
#include "tc_launch_trace.h"

// Inclusive section durations. Nested sections must not be added together.
enum class TcTransitionStep : unsigned {
    Program, Reload, BootWait, Snapshot, FilePrepare, EraseFirst, EraseSecond,
    WriteBegin, Stream, SdRead, JtagShift, WriteEnd, WaitCore, RomIdentity,
    Settings, InputRelease, FontReset, Redraw, Count
};
#ifdef TC_LAUNCH_TRACE
// Elapsed microseconds from the start of RELOAD. Zero means unobserved.
struct TcBootObservation {
    std::uint64_t start_us=0, idle_end_us=0, first_match_us=0, third_match_us=0;
    std::uint64_t last_miss_us=0, uart_idle_us=0, end_us=0, preerase_us=0;
    std::uint64_t matches=0;
    unsigned samples=0, streak=0;
    bool uart_timeout=false;
    std::uint32_t pre_id=0, pre_user=0, pre_status=0;

    void sample(bool matched, std::uint64_t now_us) {
        const auto elapsed=now_us-start_us;
        if (matched) {
            matches |= std::uint64_t(1) << samples;
            if (!first_match_us) first_match_us=elapsed;
            if (++streak==3 && !third_match_us) third_match_us=elapsed;
        } else {
            streak=0;
            last_miss_us=elapsed;
        }
        ++samples;
    }
};

struct TcBootProbe {
    enum Check : std::uint16_t {
        Snapshot=1u<<0, Id=1u<<1, User=1u<<2, Status=1u<<3,
        FifoEmpty=1u<<4, BusIdle=1u<<5, LogoNoTimeout=1u<<6, Native=1u<<7,
        MinimumWait=1u<<8, WithinDeadline=1u<<9, Stable=1u<<10, LogoAttempted=1u<<11
    };
    std::uint32_t gpio28=0, elapsed_us=0;
    std::uint16_t checks=0;

    void mark(Check check, bool passed) {
        if (passed) checks |= check;
    }
};

// One short-wait attempt per return. The full-wait retry has its own boot observation.
struct TcShortBootTrace {
    static constexpr unsigned MaxProbes=50;
    bool attempted=false, reload_started=false, wait_accepted=false;
    bool program_entered=false, configured=false;
    std::uint32_t gpio_before_select=0, gpio_after_select=0;
    std::uint32_t gpio_mask=0, gpio_expected=0, wait_end_us=0;
    unsigned probe_count=0;
    TcBootProbe probes[MaxProbes]{};
};
#endif
struct TcTransitionRecord {
#ifdef TC_LAUNCH_TRACE
    TcBootObservation boot;
    TcShortBootTrace short_boot;
#endif
    std::uint64_t origin_us=0, wall_us=0;
    std::uint64_t section_us[static_cast<unsigned>(TcTransitionStep::Count)]{};
    std::uint32_t calls[static_cast<unsigned>(TcTransitionStep::Count)]{};
    bool started=false,finished=false;
};
#ifdef TC_LAUNCH_TRACE
extern TcTransitionRecord tc_transition_records[2];
extern TcTransitionRecord *tc_transition_active;
void tc_transition_begin(unsigned phase);
void tc_transition_finish();
struct TcTransitionSection {
    TcTransitionRecord *record;
    TcTransitionStep step;
    std::uint64_t start;
    explicit TcTransitionSection(TcTransitionStep value):
        record(tc_transition_active),step(value),start(record?tc_launch_now_us():0){}
    ~TcTransitionSection(){
        if(record){
            const auto index=static_cast<unsigned>(step);
            record->section_us[index]+=tc_launch_now_us()-start;
            ++record->calls[index];
        }
    }
};
#else
inline void tc_transition_begin(unsigned){}
inline void tc_transition_finish(){}
struct TcTransitionSection {explicit TcTransitionSection(TcTransitionStep){}};
#endif
template<class Function>
auto tc_transition_call(TcTransitionStep step,Function function)->decltype(function()){
    TcTransitionSection section(step);
    return function();
}
struct TcTransitionScope {
    explicit TcTransitionScope(unsigned phase){tc_transition_begin(phase);}
    ~TcTransitionScope(){tc_transition_finish();}
};
