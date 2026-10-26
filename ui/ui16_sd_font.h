#pragma once
// Optional microSD font reader. No heap allocations and no changes to menus.
// File encoding: UI16FNT v1; bitmap bytes are top-to-bottom 16-bit BE rows.
#include <cstdint>
extern "C" {
#include "ff.h"
}

class Ui16SdFont {
public:
    Ui16SdFont() = default;
    Ui16SdFont(const Ui16SdFont &) = delete;
    Ui16SdFont &operator=(const Ui16SdFont &) = delete;
    ~Ui16SdFont() { close(); }
    // On failure, reader remains closed. Caller must use built-in font.
    bool open(const char *path);
    void close();
    bool ready() const { return opened_; }
    std::uint32_t count() const { return count_; }
    // Returns false for absent Unicode scalars AND I/O errors.
    // Do not use a partially-filled output buffer on failure.
    bool glyph(std::uint32_t codepoint, std::uint8_t (&rows)[32]);
private:
    static constexpr std::uint32_t INDEX_CAPACITY = 8192;
    bool seek_read(std::uint32_t offset, void *buffer, UINT bytes);
    FIL file_{};
    std::uint32_t count_ = 0;
    std::uint32_t index_[INDEX_CAPACITY]{};
    bool indexed_ = false;
    bool opened_ = false;
};
