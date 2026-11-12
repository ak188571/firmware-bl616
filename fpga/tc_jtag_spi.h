#pragma once
#include <cstddef>
#include <cstdint>

// Experimental Console138K-only transport. The caller owns JTAG and keeps
// interrupts masked while streaming. GPIO28 and GPIO2 are never repurposed.
enum class TcJtagSpiError : unsigned {
    None, Resources, NativePin, Clock, State, Descriptor, Dma, Fifo,
    Timeout, Incomplete, StopTimeout
};
struct TcJtagSpiStats {
    std::uint32_t dma_bytes=0, dma_blocks=0, gpio_bytes=0;
    std::uint32_t config=0, period=0;
    TcJtagSpiError error=TcJtagSpiError::None;
};
#ifdef TC_JTAG_SPI_HZ
constexpr std::uint32_t tc_jtag_spi_hz=TC_JTAG_SPI_HZ;
#endif
bool tc_jtag_spi_prepare(); // No JTAG pin mux changes; call before SRAM erase.
bool tc_jtag_spi_send(const std::uint8_t *data, std::size_t size, bool last);
bool tc_jtag_spi_close();   // Returns false if DMA cannot be quiesced.
const TcJtagSpiStats &tc_jtag_spi_stats();
