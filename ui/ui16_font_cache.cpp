#ifdef UI16_SD_FONT
#include "ui16_font_cache.h"
#include "ui16_font_map.h"
#include "ui16_font_sender.h"
#include "ui16_sd_font.h"
#include "ui16_link.h"
#include <array>
#include <cstring>

namespace {
constexpr int MAX_SLOTS=1024;
constexpr int BUILTIN_SLOTS=sizeof(ui16_font_map)/sizeof(ui16_font_map[0]);
static_assert(BUILTIN_SLOTS==319, "Update reserved range if the fixed font changes");
struct Slot {
    std::uint32_t cp=0;
    bool valid=false;
    bool needed=false;
    bool dirty=false;
    bool visible=false;
    std::uint8_t rows[32]{};
};
Ui16SdFont file;
std::array<Slot,MAX_SLOTS> slots;
bool show_pending=false;
bool frame_failed=false;
bool dynamic_in_frame=false;
bool pin_only=false;
bool raw_recovery=false;
bool verified_frame=false;
bool has_committed_frame=false;
bool need_blank=false;

void clear_cache() {
    for(int i=BUILTIN_SLOTS;i<MAX_SLOTS;++i)slots[i]=Slot{};
    has_committed_frame=false;
}
}
void ui16_font_open(const char *path) {
    file.close();clear_cache();show_pending=false;frame_failed=false;dynamic_in_frame=false;
    if (path) (void)file.open(path); // Missing/bad pack => built-in fallback.
}
void ui16_font_monitor_reset() {
    clear_cache();show_pending=false;frame_failed=false;dynamic_in_frame=false;
#ifdef UI16_RELIABLE_FONT
    ui16_link_monitor_reset();
#endif
}
void ui16_font_begin_frame() {
    frame_failed=false;dynamic_in_frame=false;raw_recovery=false;verified_frame=false;
    need_blank=!has_committed_frame;
    for(int i=BUILTIN_SLOTS;i<MAX_SLOTS;++i){slots[i].needed=false;slots[i].dirty=false;}
}
void ui16_font_pin_existing(bool enabled) { pin_only=enabled; }
std::uint16_t ui16_font_resolve(std::uint32_t cp,std::uint16_t fallback) {
    // Fixed IDs are never overwritten by the microSD cache.
    for(const auto &entry:ui16_font_map)
        if(entry.codepoint==cp)return entry.glyph;
    if(!file.ready()){if(dynamic_in_frame)frame_failed=true;return fallback;}
    for(int i=BUILTIN_SLOTS;i<MAX_SLOTS;++i)
        if(slots[i].valid && slots[i].cp==cp){slots[i].needed=true;dynamic_in_frame=true;return std::uint16_t(i);}
    if(pin_only)return fallback;
    // Check that this Unicode scalar exists before assigning a physical ID.
    std::uint8_t rows[32];
    if(!file.glyph(cp,rows)){if(!file.ready())frame_failed=true;return fallback;}
    int target=-1;
    for(int i=BUILTIN_SLOTS;i<MAX_SLOTS;++i)
        if(!slots[i].valid){target=i;break;}
    // Prefer a slot absent from the currently displayed frame. Uploading to
    // that slot cannot change a visible glyph, so the display can stay on.
    if(target<0)for(int i=BUILTIN_SLOTS;i<MAX_SLOTS;++i)
        if(!slots[i].needed && !slots[i].visible){target=i;break;}
    if(target<0)for(int i=BUILTIN_SLOTS;i<MAX_SLOTS;++i)
        if(!slots[i].needed){target=i;break;}
    if(target<0)return fallback; // No spare slot; never evict a glyph used in this frame.
    if(slots[target].visible)need_blank=true;
    slots[target]=Slot{};
    slots[target].cp=cp;
    slots[target].valid=true;
    slots[target].needed=true;
    slots[target].dirty=true;
    std::memcpy(slots[target].rows,rows,32);
    dynamic_in_frame=true;
    return std::uint16_t(target);
}
bool ui16_font_before_cells() {
    if(frame_failed){clear_cache();return false;}
    if(!file.ready())return true; // Old monitor-compatible fixed-font path.
#ifdef UI16_RELIABLE_FONT
    if(!ui16_link_prepare()) { clear_cache();return false; }
#endif
    verified_frame=true;
    if(need_blank){
        if(!ui16_font_blank()) {
            clear_cache();raw_recovery=true;show_pending=true;
            return false;
        }
        show_pending=true;
    }
    for(int i=BUILTIN_SLOTS;i<MAX_SLOTS;++i)if(slots[i].dirty) {
        if(!ui16_font_write(std::uint16_t(i),slots[i].rows)) {
            clear_cache();raw_recovery=true;show_pending=true;return false;
        }
        slots[i].dirty=false;
    }
    return true;
}
bool ui16_font_frame_gated() { return verified_frame; }
bool ui16_font_raw_recovery() { return raw_recovery; }
void ui16_font_abort_frame() { clear_cache();raw_recovery=true;show_pending=true; }
void ui16_font_start_raw_recovery() { if(raw_recovery && show_pending)ui16_font_raw_blank(); }
bool ui16_font_after_cells() {
    if(raw_recovery){
        if(show_pending)ui16_font_raw_show();
        show_pending=false;verified_frame=false;return true;
    }
    if(show_pending){
        show_pending=false;
        if(!ui16_font_show())return false;
    }
    if(verified_frame){
        for(int i=BUILTIN_SLOTS;i<MAX_SLOTS;++i)slots[i].visible=slots[i].valid && slots[i].needed;
        has_committed_frame=true;
    }
    verified_frame=false;
    return true;
}
#endif
