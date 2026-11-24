#pragma once
#include <cstdint>
#include <array>
#include <string_view>

struct Ui16CrtSettings {
    bool enabled=false;
    unsigned blur=0,scanline=0;
    bool beam=false;
    unsigned mask=0,gamma=0,temperature=0;
    bool vignette=false,rounded=false,overscan=false,bleed=false;
    std::array<std::uint8_t,4> widths{}; // top, bottom, left, right, in output pixels

    bool glow=false;
    // The original blur and bleed fields are the RGB bank, including legacy records.
    bool composite=false;
    unsigned composite_blur=1;
    bool composite_bleed=true;
    bool curvature=false;
    bool convergence=false;
    static constexpr std::uint32_t ConvergenceBit=1u<<13;
    static constexpr std::uint32_t CurveBit=1u<<14;
    bool needs_signal_record() const {return composite || composite_blur!=1 || !composite_bleed;}
    unsigned signal_blur() const {return composite ? composite_blur : blur;}
    bool signal_bleed() const {return composite ? composite_bleed : bleed;}
    static constexpr std::uint32_t GlowBit=1u<<15;

    bool valid() const {return composite_blur<4 && blur<4 && scanline<4 && mask<3 && gamma<3 && temperature<3;}
    std::uint16_t flags() const {
        return enabled | (blur<<1) | (scanline<<3) | (beam<<5) | (mask<<6) |
               (gamma<<8) | (temperature<<10) | (vignette<<12) | (rounded<<13) |
               (overscan<<14) | (bleed<<15);
    }
    static Ui16CrtSettings from_flags(unsigned bits) {
        Ui16CrtSettings v{bool(bits&1), (bits>>1)&3, (bits>>3)&3};
        v.beam=bits&0x20; v.mask=(bits>>6)&3; v.gamma=(bits>>8)&3; v.temperature=(bits>>10)&3;
        v.vignette=bits&0x1000; v.rounded=bits&0x2000; v.overscan=bits&0x4000; v.bleed=bits&0x8000;
        return v;
    }
    std::uint32_t merge(std::uint32_t config,bool extended=false,bool supports_glow=false,bool supports_curve=false,bool supports_convergence=false)const {
        const std::uint32_t mask=extended?0xffff0000u:0x001f0000u;
        if(supports_convergence)config=(config&~ConvergenceBit)|(convergence?ConvergenceBit:0u);
        if(supports_curve)config=(config&~CurveBit)|(curvature?CurveBit:0u);
        if(supports_glow)config=(config&~GlowBit)|(glow?GlowBit:0u);
        auto active=flags();
        if(extended && composite){
            active=(active & ~0x8006u) | (composite_blur<<1) | (unsigned(composite_bleed)<<15);
        }
        return (config&~mask)|((std::uint32_t(active)<<16)&mask);
    }
    bool operator==(const Ui16CrtSettings &v)const{return flags()==v.flags() && widths==v.widths && glow==v.glow &&
        convergence==v.convergence && curvature==v.curvature && composite==v.composite && composite_blur==v.composite_blur && composite_bleed==v.composite_bleed;}
    bool operator!=(const Ui16CrtSettings &v)const{return !(*this==v);}
};
struct Ui16CrtRecommendation {bool present=false;Ui16CrtSettings settings;};
// Versioned titles/defaults field: enable:blur:scanline, or empty to inherit.
inline bool ui16_parse_crt(std::string_view text,Ui16CrtRecommendation &out){
    out={};if(text.empty())return true;
    if(text.size()!=5||text[0]<'0'||text[0]>'1'||text[1]!=':'||text[2]<'0'||text[2]>'3'||text[3]!=':'||text[4]<'0'||text[4]>'3')return false;
    out.present=true;out.settings={text[0]=='1',unsigned(text[2]-'0'),unsigned(text[4]-'0')};return true;
}
