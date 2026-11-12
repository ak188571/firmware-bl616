#pragma once
#include <cstdint>
#include <cstddef>
inline constexpr std::uint32_t tc_spi_data_hz=32000000;
bool tc_spi_open();
bool tc_spi_send(const std::uint8_t *frame, std::size_t size, std::uint32_t timeout_ms,
                 void (*pump)(void *), void *pump_context);
void tc_spi_close();
