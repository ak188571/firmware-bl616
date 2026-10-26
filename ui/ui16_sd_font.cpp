#include "ui16_sd_font.h"
#include <cstring>

namespace {
constexpr std::uint32_t HEADER_SIZE = 32;
constexpr std::uint32_t RECORD_SIZE = 36;
constexpr std::uint32_t MAX_GLYPHS = 65535;
std::uint16_t le16(const std::uint8_t *p) {
    return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
}
std::uint32_t le32(const std::uint8_t *p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
bool valid_scalar(std::uint32_t cp) {
    return cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
}
std::uint32_t crc32_byte(std::uint32_t crc, std::uint8_t b) {
    crc ^= b;
    for (unsigned i=0;i<8;++i) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    return crc;
}
}

bool Ui16SdFont::seek_read(std::uint32_t offset, void *buffer, UINT bytes) {
    if (f_lseek(&file_, offset) != FR_OK) return false;
    UINT actual = 0;
    return f_read(&file_, buffer, bytes, &actual) == FR_OK && actual == bytes;
}

bool Ui16SdFont::open(const char *path) {
    close();
    if (!path || f_open(&file_, path, FA_READ) != FR_OK) return false;
    // Mark open immediately, so every error path closes the file.
    opened_ = true;
    std::uint8_t header[HEADER_SIZE]{};
    if (!seek_read(0, header, HEADER_SIZE) ||
        std::memcmp(header, "UI16FNT\0", 8) != 0 ||
        le16(header+8) != 1 || header[10] != 16 || header[11] != 16 ||
        le32(header+20) != HEADER_SIZE || le32(header+24) != 0 || le32(header+28) != 0) {
        close(); return false;
    }
    const auto count = le32(header+12);
    const auto expected = HEADER_SIZE + count * RECORD_SIZE;
    if (!count || count > MAX_GLYPHS ||
        expected != f_size(&file_)) { close(); return false; }
    // Validate all bytes (including ordering and CRC) before accepting the file.
    std::uint8_t record[RECORD_SIZE];
    std::uint32_t previous = 0;
    std::uint32_t crc = 0xffffffffu;
    if (f_lseek(&file_, HEADER_SIZE) != FR_OK) { close(); return false; }
    for (std::uint32_t i=0;i<count;++i) {
        UINT actual = 0;
        if (f_read(&file_, record, RECORD_SIZE, &actual) != FR_OK || actual != RECORD_SIZE) {
            close(); return false;
        }
        const auto cp = le32(record);
        if (!valid_scalar(cp) || (i && cp <= previous)) { close(); return false; }
        if (count <= INDEX_CAPACITY) index_[i] = cp;
        previous = cp;
        for (auto b : record) crc = crc32_byte(crc,b);
    }
    if ((crc ^ 0xffffffffu) != le32(header+16)) { close(); return false; }
    count_ = count;
    indexed_ = count <= INDEX_CAPACITY;
    return true;
}

bool Ui16SdFont::glyph(std::uint32_t codepoint, std::uint8_t (&rows)[32]) {
    if (!opened_ || !valid_scalar(codepoint)) return false;
    std::uint32_t lo=0, hi=count_;
    std::uint8_t record[RECORD_SIZE]{};
    if (indexed_) {
        while (lo < hi) {
            const auto mid=lo+(hi-lo)/2;
            if (index_[mid] < codepoint) lo=mid+1;
            else hi=mid;
        }
        if (lo == count_ || index_[lo] != codepoint) return false;
        if (!seek_read(HEADER_SIZE+lo*RECORD_SIZE, record, RECORD_SIZE)) {
            close(); return false;
        }
        if (le32(record) != codepoint) { close(); return false; }
        std::memcpy(rows,record+4,32);
        return true;
    }
    while(lo<hi) {
        const auto mid=lo+(hi-lo)/2;
        if (!seek_read(HEADER_SIZE+mid*RECORD_SIZE, record, RECORD_SIZE)) {
            close(); return false;
        }
        const auto found=le32(record);
        if (found == codepoint) {
            std::memcpy(rows, record+4, 32);
            return true;
        }
        if (found < codepoint) lo=mid+1;
        else hi=mid;
    }
    return false;
}

void Ui16SdFont::close() {
    if (opened_) f_close(&file_);
    opened_=false;
    count_=0;
    indexed_=false;
}
