#pragma once
#include <cstdint>

enum class Ui16GameOsd { None, System, Crt };
constexpr std::uint16_t UI16_OSD1_KEY=0x0084; // SELECT + RIGHT
constexpr std::uint16_t UI16_OSD2_KEY=0x0044; // SELECT + LEFT
constexpr std::uint32_t UI16_CRT_MASK=0x001f0000;

// Inspect each source independently: buttons from different devices do not
// combine into a hotkey. Opposite hotkeys are reserved but open neither OSD.
inline bool ui16_osd_reserved(std::uint16_t pad) {
    return (pad&UI16_OSD1_KEY)==UI16_OSD1_KEY ||
           (pad&UI16_OSD2_KEY)==UI16_OSD2_KEY;
}
inline Ui16GameOsd ui16_osd_request(std::uint16_t direct,std::uint16_t hid) {
    const bool right=(direct&UI16_OSD1_KEY)==UI16_OSD1_KEY ||
                     (hid&UI16_OSD1_KEY)==UI16_OSD1_KEY;
    const bool left=(direct&UI16_OSD2_KEY)==UI16_OSD2_KEY ||
                    (hid&UI16_OSD2_KEY)==UI16_OSD2_KEY;
    return right==left?Ui16GameOsd::None:right?Ui16GameOsd::System:Ui16GameOsd::Crt;
}
struct Ui16CrtSettings {
    bool enabled=false;
    unsigned blur=0,scanline=0;
    std::uint32_t merge(std::uint32_t config) const {
        return (config&~UI16_CRT_MASK)|(std::uint32_t(enabled)<<16)|
               ((blur&3u)<<17)|((scanline&3u)<<19);
    }
};

struct Menu;
Menu *ui16_create_crt_menu(bool horizontal_blur=false);
void ui16_crt_new_session();
void ui16_crt_accept_status(std::uint32_t applied);
