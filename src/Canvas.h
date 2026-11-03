#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>

#include <cstdint>
#include <map>
#include <string>

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    Rect inset(float d) const { return Rect{x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(float dx, float dy) const { return Rect{x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    Rect offset(float dx, float dy) const { return Rect{x + dx, y + dy, w, h}; }
    Rect intersect(const Rect& o) const {
        float l = x > o.x ? x : o.x;
        float t = y > o.y ? y : o.y;
        float r = right() < o.right() ? right() : o.right();
        float b = bottom() < o.bottom() ? bottom() : o.bottom();
        return Rect{l, t, r - l > 0 ? r - l : 0, b - t > 0 ? b - t : 0};
    }
};

using Color = uint32_t;

// Direct2D renderer that draws into a DirectComposition swap chain so the
// window can show the Windows 11 Mica backdrop through translucent surfaces.
class Canvas {
public:
    Canvas() = default;
    ~Canvas();

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    bool create(HWND hwnd, float dpi);
    void discard();
    bool resize(UINT pixelWidth, UINT pixelHeight, float dpi);

    bool begin();
    bool end();

    void clear(Color color);
    void fillRect(const Rect& rect, Color color);
    void fillRound(const Rect& rect, float radius, Color color);
    void strokeRound(const Rect& rect, float radius, Color color, float width = 1.0f);
    void line(float x1, float y1, float x2, float y2, Color color, float width = 1.0f);
    void pushClip(const Rect& rect);
    void popClip();

    void text(const Rect& rect, const std::wstring& value, float size, Color color,
              DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
              DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
              DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
              bool ellipsis = true);

    void glyph(const Rect& rect, wchar_t codepoint, float size, Color color,
               DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_CENTER);

    float textWidth(const std::wstring& value, float size,
                    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);
    float lineHeight(float size) const { return size * 1.38f; }

    float dpi() const { return dpi_; }
    bool ready() const { return target_ != nullptr; }
    bool composed() const { return swapChain_ != nullptr; }
    const std::wstring& textFamily() const { return textFamily_; }

private:
    struct FormatKey {
        int size;
        int weight;
        int align;
        int valign;
        bool icons;
        bool operator<(const FormatKey& o) const {
            if (size != o.size) return size < o.size;
            if (weight != o.weight) return weight < o.weight;
            if (align != o.align) return align < o.align;
            if (valign != o.valign) return valign < o.valign;
            return icons < o.icons;
        }
    };

    IDWriteTextFormat* formatFor(float size, DWRITE_FONT_WEIGHT weight,
                                 DWRITE_TEXT_ALIGNMENT align,
                                 DWRITE_PARAGRAPH_ALIGNMENT valign, bool icons);
    ID2D1SolidColorBrush* brushFor(Color color);
    IDWriteInlineObject* ellipsisSign(IDWriteTextFormat* format);
    void releaseBrushes();
    bool createDeviceResources();
    bool createSwapChain();
    bool createTargetBitmap();
    void releaseTargets();

    HWND hwnd_ = nullptr;
    ID2D1Factory1* factory_ = nullptr;
    IDWriteFactory* dwrite_ = nullptr;
    ID3D11Device* d3d_ = nullptr;
    IDXGIDevice* dxgiDevice_ = nullptr;
    ID2D1Device* d2dDevice_ = nullptr;
    ID2D1DeviceContext* target_ = nullptr;
    IDXGISwapChain1* swapChain_ = nullptr;
    IDCompositionDevice* composition_ = nullptr;
    IDCompositionTarget* compositionTarget_ = nullptr;
    IDCompositionVisual* compositionVisual_ = nullptr;
    ID2D1Bitmap1* targetBitmap_ = nullptr;
    IDWriteInlineObject* ellipsis_ = nullptr;
    std::wstring textFamily_ = L"Segoe UI";
    std::wstring displayFamily_ = L"Segoe UI";
    std::wstring iconFamily_ = L"Segoe MDL2 Assets";
    std::map<FormatKey, IDWriteTextFormat*> formats_;
    std::map<Color, ID2D1SolidColorBrush*> brushes_;
    UINT pixelWidth_ = 0;
    UINT pixelHeight_ = 0;
    float dpi_ = 96.0f;
    int clipDepth_ = 0;
};

inline D2D1_RECT_F ToD2D(const Rect& r) {
    return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h);
}
