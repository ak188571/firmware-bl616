#ifdef UI16_SYSTEM_MENU
#include "ui16_system_menu.h"
#include "ui16_font_map.h"
#include "utils.h"
#include <string_view>

namespace {
// Same UTF-8 validation and cell widths as the PC simulator.
std::uint32_t next_utf8(std::string_view s, std::size_t &pos)
{
    const auto first = static_cast<unsigned char>(s[pos]);
    if (first < 0x80) { ++pos; return first; }
    const int n = first >= 0xc2 && first <= 0xdf ? 2 :
                  first >= 0xe0 && first <= 0xef ? 3 :
                  first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (!n || pos + n > s.size()) { ++pos; return 0xfffd; }
    std::uint32_t cp = first & (0x7f >> n);
    for (int i = 1; i < n; ++i) {
        const auto c = static_cast<unsigned char>(s[pos + i]);
        if ((c & 0xc0) != 0x80) { ++pos; return 0xfffd; }
        cp = (cp << 6) | (c & 0x3f);
    }
    if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) ||
        (n == 4 && cp < 0x10000) || (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) {
        ++pos; return 0xfffd;
    }
    pos += n;
    return cp;
}

void text(std::uint16_t *row, const char *label, bool selected)
{
    if (!label) return;
    const std::string_view s(label);
    int x = 2;
    std::size_t pos = 0;
    while (pos < s.size()) {
        auto cp = next_utf8(s, pos);
        // The prototype font contains uppercase Latin letters only.
        if (cp >= 'a' && cp <= 'z') cp -= 'a' - 'A';
        const int width = cp < 0x80 ? 1 : 2;
        if (x + width > TEXT_UNITS_PER_ROW) break; // Never split a fullwidth glyph.
        std::uint16_t glyph = 0;
        for (const auto &g : ui16_font_map)
            if (g.codepoint == cp) { glyph = g.glyph; break; }
        const std::uint16_t attr = selected ? 1u << (8 + ATTR_SELECTED_BIT) : 0;
        row[x++] = glyph | attr;
        if (width == 2) row[x++] = glyph | attr | (1u << (8 + ATTR_HALF_BIT));
    }
}

const char *label_for(const Ui16MenuEntry &entry)
{
    switch (entry.id) {
    case 1: return u8"ファミリーコンピュータ";
    case 2: return u8"スーパーファミコン";
    default: return entry.name;
    }
}
}

void ui16_build_system_menu(const Ui16MenuEntry *entries, std::size_t count,
                           std::uint16_t (&cells)[CELL_COUNT])
{
    for (auto &cell : cells) cell = 0;
    text(cells + TEXT_UNITS_PER_ROW, "SYSTEMS", false);
    bool selected = false;
    int row = UI16_MENU_FIRST_ROW;
    for (std::size_t i = 0; entries && i < count && row < MENU_ROWS - 1; ++i) {
        if (entries[i].id == 0) break;
        const bool select = entries[i].id > 0 && !selected;
        selected |= select;
        text(cells + row++ * TEXT_UNITS_PER_ROW, label_for(entries[i]), select);
    }
}

void ui16_send_system_menu(const Ui16MenuEntry *entries, std::size_t count)
{
    std::uint16_t cells[CELL_COUNT];
    ui16_build_system_menu(entries, count, cells);
    for (int y = 0; y < MENU_ROWS; ++y) {
        taskENTER_CRITICAL();
        fpga_tx_header(ROW_COMMAND, 4 + 2 * TEXT_UNITS_PER_ROW);
        fpga_tx_byte(static_cast<std::uint8_t>(y));
        fpga_tx_byte(0);
        fpga_tx_byte(TEXT_UNITS_PER_ROW);
        for (int x = 0; x < TEXT_UNITS_PER_ROW; ++x) {
            const auto cell = cells[y * TEXT_UNITS_PER_ROW + x];
            fpga_tx_byte(static_cast<std::uint8_t>(cell & 255));
            fpga_tx_byte(static_cast<std::uint8_t>((cell >> 8) & ATTR_MASK));
        }
        taskEXIT_CRITICAL();
    }
}
#endif
