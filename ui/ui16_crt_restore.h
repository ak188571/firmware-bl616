#pragma once
#include "ui16_rom_id.h"
#include "ui16_crt_settings.h"

// Version 2 is reserved by the deployed unknown-schema compatibility fixtures.
constexpr unsigned UI16_CRT_EXT_RECORD=3;
// Same payload as schema 3; schema 4 identifies a USER record with Glow enabled.
constexpr unsigned UI16_CRT_GLOW_RECORD=4;

// Schema 5 retains the 32-byte slot and CRC layout. Byte 5 holds NES/SNES ID
// in bits 1:0, selected signal in bit 2, Composite blur in 4:3, Composite
// bleed in bit 5 and shared Glow in bit 6. Bit 7 is reserved. Flags in 6:7
// keep both RGB controls; widths and the full ROM key/generation are unchanged.
constexpr unsigned UI16_CRT_SIGNAL_RECORD=5;
// Same packed profile as schema 5 with curvature enabled. Older readers protect unknown slots.
constexpr unsigned UI16_CRT_CURVE_RECORD=6;
// Schema 7 implies convergence. Byte 5 bit 7 independently stores curvature.
constexpr unsigned UI16_CRT_CONVERGENCE_RECORD=7;
// NTSC implies Composite; byte 5 bit 2 is available for convergence in this format.
constexpr unsigned UI16_CRT_NTSC_RECORD=8;
// Setting bit 4 of the schema byte on extended USER formats 3 through 8
// denotes Bloom. Their original payload and the full 32-byte CRC remain intact.
constexpr unsigned UI16_CRT_BLOOM_FLAG=0x10;
// Bit 5 independently marks afterglow in the same extended USER records.
constexpr unsigned UI16_CRT_PERSISTENCE_FLAG=0x20;
// Bit 6 marks beam spot on extended USER records. Older readers protect these slots.
constexpr unsigned UI16_CRT_SPOT_FLAG=0x40;
// FAT32 has 32-bit file sizes. In display-extension records bytes 16..19
// hold display options; bytes 12..15 hold the complete on-volume ROM size.
constexpr unsigned UI16_CRT_DISPLAY_FLAG=0x80;
constexpr unsigned UI16_CRT_SIGNAL_SYSTEM_MASK=3;

enum class Ui16CrtReadState { NoId, Missing, User, Reset, Recovered, Bad, Io, Future, Conflict };
struct Ui16CrtRestore {
    Ui16CrtReadState state=Ui16CrtReadState::NoId;
    Ui16CrtRecommendation override_value;
    int selected_slot=-1;
    std::uint32_t generation=0;
};
inline const char *ui16_crt_read_name(Ui16CrtReadState state){
    switch(state){
    case Ui16CrtReadState::Missing:return "MISSING";
    case Ui16CrtReadState::User:return "USER";
    case Ui16CrtReadState::Reset:return "RESET";
    case Ui16CrtReadState::Recovered:return "RECOVERED";
    case Ui16CrtReadState::Bad:return "BAD SAVE";
    case Ui16CrtReadState::Io:return "READ ERROR";
    case Ui16CrtReadState::Future:return "NEW SCHEMA";
    case Ui16CrtReadState::Conflict:return "CONFLICT";
    default:return "NO ROM ID";
    }
}
