#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

struct Colors {
    uint32_t windowBg;
    uint32_t sidebarBg;
    uint32_t contentBg;
    uint32_t cardBg;
    uint32_t cardHover;
    uint32_t stroke;
    uint32_t strokeStrong;
    uint32_t text;
    uint32_t textDim;
    uint32_t textFaint;
    uint32_t accent;
    uint32_t accentHover;
    uint32_t navSelected;
    uint32_t onAccent;
    uint32_t success;
    uint32_t warning;
    uint32_t danger;
    uint32_t dangerSoft;
    uint32_t fieldBg;
    uint32_t fieldStroke;
    uint32_t selection;
    uint32_t scrollThumb;
    uint32_t captionHover;
    uint32_t closeHover;
    uint32_t closeHoverText;
    uint32_t shadow;
    uint32_t overlay;
};

inline uint32_t Blend(uint32_t base, uint32_t over, float amount) {
    auto mix = [amount](uint32_t a, uint32_t b) {
        return (uint32_t)((float)a + ((float)b - (float)a) * amount + 0.5f);
    };
    uint32_t r = mix((base >> 16) & 0xFF, (over >> 16) & 0xFF);
    uint32_t g = mix((base >> 8) & 0xFF, (over >> 8) & 0xFF);
    uint32_t b = mix(base & 0xFF, over & 0xFF);
    uint32_t a = mix((base >> 24) & 0xFF, (over >> 24) & 0xFF);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

inline uint32_t WithAlpha(uint32_t color, uint32_t alpha) {
    return (color & 0x00FFFFFFu) | (alpha << 24);
}

inline uint32_t HslToRgb(float h, float s, float l) {
    auto hue = [](float p, float q, float t) {
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
        if (t < 1.0f / 2.0f) return q;
        if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        return p;
    };
    float r, g, b;
    if (s <= 0.0f) {
        r = g = b = l;
    } else {
        float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        float hk = h / 360.0f;
        r = hue(p, q, hk + 1.0f / 3.0f);
        g = hue(p, q, hk);
        b = hue(p, q, hk - 1.0f / 3.0f);
    }
    auto to8 = [](float v) { return (uint32_t)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return 0xFF000000u | (to8(r) << 16) | (to8(g) << 8) | to8(b);
}

inline void RgbToHsl(uint32_t c, float& h, float& s, float& l) {
    float r = ((c >> 16) & 0xFF) / 255.0f;
    float g = ((c >> 8) & 0xFF) / 255.0f;
    float b = (c & 0xFF) / 255.0f;
    float mx = std::max({r, g, b});
    float mn = std::min({r, g, b});
    l = (mx + mn) * 0.5f;
    if (mx == mn) {
        h = s = 0.0f;
        return;
    }
    float d = mx - mn;
    s = l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
    if (mx == r)
        h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g)
        h = (b - r) / d + 2.0f;
    else
        h = (r - g) / d + 4.0f;
    h *= 60.0f;
}

// Windows uses a lighter accent shade for controls in dark mode so black text
// stays readable, and a slightly darker one on hover in light mode.
inline uint32_t AccentShade(uint32_t accent, float lightness) {
    float h, s, l;
    RgbToHsl(accent, h, s, l);
    return HslToRgb(h, s, lightness);
}

// Windows 11 "Mica" palette built from the WinUI 3 theme tokens. The base
// window layer stays fully transparent so the DWM backdrop (the blurred
// desktop wallpaper) shows through; the content/sidebar layers use the
// low-opacity LayerFill tokens from the WinUI layering guidance.
inline Colors DarkColors(bool translucent, uint32_t accent) {
    const uint32_t accentFill = AccentShade(accent, 0.69f);  // ~ SystemAccentColorLight2
    const uint32_t accentHover = AccentShade(accent, 0.76f);
    return Colors{
        translucent ? 0x00000000u : 0xFF202020u, // windowBg (Mica base)
        translucent ? 0x08FFFFFFu : 0xFF1C1C1Cu, // sidebarBg
        translucent ? 0x0DFFFFFFu : 0xFF202020u, // contentBg (LayerFillColorDefault)
        translucent ? 0x14FFFFFFu : 0xFF262626u, // cardBg
        translucent ? 0x1FFFFFFFu : 0xFF2E2E2Eu, // cardHover
        0x0FFFFFFFu,                             // stroke
        0x1AFFFFFFu,                             // strokeStrong
        0xFFFFFFFFu,                             // text
        0xC5FFFFFFu,                             // textDim
        0x87FFFFFFu,                             // textFaint
        accentFill,
        accentHover,
        0x0FFFFFFFu, // navSelected
        0xFF000000u, // onAccent
        0xFF6CCB5Fu, // success
        0xFFFFD54Fu, // warning
        0xFFFF99A4u, // danger
        0x26FF99A4u, // dangerSoft
        translucent ? 0x14FFFFFFu : 0xFF2A2A2Au, // fieldBg
        0x1FFFFFFFu,                             // fieldStroke
        WithAlpha(accentFill, 0x55),             // selection
        0x40FFFFFFu,                             // scrollThumb
        0x0FFFFFFFu,                             // captionHover
        0xFFC42B1Cu,                             // closeHover
        0xFFFFFFFFu,                             // closeHoverText
        0x66000000u,                             // shadow
        0x99000000u,                             // overlay
    };
}

inline Colors LightColors(bool translucent, uint32_t accent) {
    const uint32_t accentFill = accent | 0xFF000000u;
    float h, s, l;
    RgbToHsl(accent, h, s, l);
    const uint32_t accentHover = AccentShade(accent, std::max(0.30f, l * 0.88f));
    return Colors{
        translucent ? 0x00000000u : 0xFFF3F3F3u, // windowBg (Mica base)
        translucent ? 0x26FFFFFFu : 0xFFE9E9E9u, // sidebarBg
        translucent ? 0x8CFFFFFFu : 0xFFF3F3F3u, // contentBg (LayerFillColorDefault)
        translucent ? 0xB3FFFFFFu : 0xFFFFFFFFu, // cardBg
        translucent ? 0xD9FFFFFFu : 0xFFFAFAFAu, // cardHover
        0x0F000000u,                             // stroke
        0x1F000000u,                             // strokeStrong
        0xE4000000u,                             // text
        0x9E000000u,                             // textDim
        0x72000000u,                             // textFaint
        accentFill,
        accentHover,
        0x09000000u, // navSelected
        0xFFFFFFFFu, // onAccent
        0xFF0F7B0Fu, // success
        0xFF9D5D00u, // warning
        0xFFC42B1Cu, // danger
        0x1AC42B1Cu, // dangerSoft
        translucent ? 0x66FFFFFFu : 0xFFF7F7F7u, // fieldBg
        0x1A000000u,                             // fieldStroke
        WithAlpha(accentFill, 0x33),             // selection
        0x33000000u,                             // scrollThumb
        0x0F000000u,                             // captionHover
        0xFFC42B1Cu,                             // closeHover
        0xFFFFFFFFu,                             // closeHoverText
        0x33000000u,                             // shadow
        0x66000000u,                             // overlay
    };
}
