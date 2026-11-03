#pragma once
#include <cstdint>
#include <cstddef>

enum class Ui16RomVolume { None, Sd, Usb };
struct Ui16RomIdentity {
    bool valid=false;
    Ui16RomVolume volume=Ui16RomVolume::None;
    unsigned system=0;
    std::uint32_t crc=0;
    std::uint64_t size=0;
    bool same_game(const Ui16RomIdentity &other)const {
        return valid&&other.valid&&volume==other.volume&&system==other.system&&crc==other.crc&&size==other.size;
    }
};

// CRC32/IEEE reflected, initial state 0xffffffff and final XOR 0xffffffff.
std::uint32_t ui16_rom_crc(std::uint32_t state,const void *data,std::size_t size);
