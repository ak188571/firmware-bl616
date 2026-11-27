#pragma once
#include <cstdint>

// Checked modes may proceed after this minimum and three consecutive ready probes.
enum class MonitorBootWait : std::uint16_t {
    Full = 5000,
    Checked500ms = 500,
    Checked1s = 1000,
};
