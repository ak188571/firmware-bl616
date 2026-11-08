#pragma once
#include "tc_rom_profile.h"
enum class TcRomMeasurePhase : unsigned {Data,End,Count};
struct TcRomMeasureRecord {
    std::uint64_t elapsed_us=0;
    std::uint64_t us[TcRom::ActivityCount]{},max_us[TcRom::ActivityCount]{};
    std::uint32_t calls[TcRom::ActivityCount]{},units[TcRom::ActivityCount]{};
    std::uint64_t tx_envelope_us=0,tx_gap_us=0,tx_gap_max_us=0,tx_span_us=0,sd_in_send_us=0;
    std::uint32_t tx_frames=0,tx_bytes=0,tx_failures=0,ack_calls=0,ack_ready=0,ack_empty_polls=0;
};
#ifdef TC_ROM_PROFILE
TcRom::Profile *tc_rom_measure_observer();
void tc_rom_measure_prepare();
void tc_rom_measure_begin(TcRomMeasurePhase);
void tc_rom_measure_end();
void tc_rom_measure_tx_begin(std::size_t);
void tc_rom_measure_tx_end(bool);
void tc_rom_measure_ack_begin();
void tc_rom_measure_ack_poll(int,bool);
#else
inline TcRom::Profile *tc_rom_measure_observer(){return nullptr;}
inline void tc_rom_measure_prepare(){}
inline void tc_rom_measure_begin(TcRomMeasurePhase){}
inline void tc_rom_measure_end(){}
inline void tc_rom_measure_tx_begin(std::size_t){}
inline void tc_rom_measure_tx_end(bool){}
inline void tc_rom_measure_ack_begin(){}
inline void tc_rom_measure_ack_poll(int,bool){}
#endif
