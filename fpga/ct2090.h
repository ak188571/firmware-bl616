#pragma once
#include <cstdint>

namespace Ct2090 {
struct Snapshot {
    std::uint32_t id, user, status;
};
constexpr std::uint32_t Device = 0x1081b;
constexpr std::uint32_t Boot = 0x3ae8;
constexpr std::uint32_t Monitor = 0x8883;
constexpr std::uint32_t BootStatus = 0x26020;
constexpr std::uint32_t Done = 1u << 13;

// A failed or interrupted operation consumes the previous monitor proof.
// A fresh MCU boot always starts without that proof.
template<class Port>
bool configure(Port& port, bool monitor_target, bool& monitor_confirmed) {
    const bool was_monitor = monitor_confirmed;
    monitor_confirmed = false;
    Snapshot before{};
    if (!port.snapshot(before) || before.id != Device) return false;

    const bool leaving_monitor = was_monitor && before.user == Monitor &&
                                 (before.status & Done) && !monitor_target;
    Snapshot expected = before;
    if (!leaving_monitor) {
        if (!port.reload()) return false;
        Snapshot boot{};
        if (!port.snapshot(boot) || boot.id != Device ||
            boot.user != Boot || boot.status != BootStatus) return false;
        expected = boot;
    }
    if (!port.program(expected)) return false;
    Snapshot loaded{};
    if (!port.snapshot(loaded) || loaded.id != Device || !(loaded.status & Done))
        return false;
    if (monitor_target && loaded.user != Monitor) return false;
    monitor_confirmed = monitor_target;
    return true;
}
}
