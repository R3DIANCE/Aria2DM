#include "Canvas.h"

#include <algorithm>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

namespace {

D2D1_COLOR_F ToColor(Color c) {
    return D2D1::ColorF(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f,
                        ((c >> 24) & 0xFF) / 255.0f);
}

template <typename T>
void SafeRelease(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

} // namespace

Canvas::~Canvas() {
    discard();
    for (auto& kv : formats_) kv.second->Release();
    formats_.clear();
    SafeRelease(ellipsis_);
    SafeRelease(compositionTarget_);
    SafeRelease(compositionVisual_);
    SafeRelease(composition_);
    SafeRelease(d2dDevice_);
    SafeRelease(dxgiDevice_);
    SafeRelease(d3d_);
    SafeRelease(factory_);
    SafeRelease(dwrite_);
}

bool Canvas::create(HWND hwnd, float dpi) {
    hwnd_ = hwnd;
    dpi_ = dpi > 0 ? dpi : 96.0f;

    if (!factory_) {
        D2D1_FACTORY_OPTIONS options{};
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                       &options, reinterpret_cast<void**>(&factory_));
        if (FAILED(hr)) return false;
    }
    if (!dwrite_) {
        HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown**>(&dwrite_));
        if (FAILED(hr)) return false;
    }

    IDWriteFontCollection* collection = nullptr;
    if (SUCCEEDED(dwrite_->GetSystemFontCollection(&collection, FALSE)) && collection) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(collection->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists) {
            textFamily_ = L"Segoe UI Variable Text";
            displayFamily_ = textFamily_;
        }
        exists = FALSE;
        if (SUCCEEDED(collection->FindFamilyName(L"Segoe UI Variable Display", &index, &exists)) && exists)
            displayFamily_ = L"Segoe UI Variable Display";
        exists = FALSE;
        if (SUCCEEDED(collection->FindFamilyName(L"Segoe Fluent Icons", &index, &exists)) && exists)
            iconFamily_ = L"Segoe Fluent Icons";
        collection->Release();
    }

    if (!createDeviceResources()) return false;
    if (!createSwapChain()) return false;

    if (composition_) {
        compositionTarget_->SetRoot(compositionVisual_);
        composition_->Commit();
    }
    return true;
}

bool Canvas::createDeviceResources() {
    if (target_) return true;
    if (!factory_) return false;

    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                  D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                   (UINT)std::size(levels), D3D11_SDK_VERSION, &d3d_, &obtained,
                                   nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                               (UINT)std::size(levels), D3D11_SDK_VERSION, &d3d_, &obtained, nullptr);
    }
    if (FAILED(hr) || !d3d_) return false;

    hr = d3d_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice_));
    if (FAILED(hr) || !dxgiDevice_) return false;

    hr = factory_->CreateDevice(dxgiDevice_, &d2dDevice_);
    if (FAILED(hr) || !d2dDevice_) return false;

    hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &target_);
    if (FAILED(hr) || !target_) return false;

    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target_->SetDpi(dpi_, dpi_);

    HRESULT compositionResult =
        DCompositionCreateDevice(dxgiDevice_, __uuidof(IDCompositionDevice),
                                 reinterpret_cast<void**>(&composition_));
    if (SUCCEEDED(compositionResult) && composition_) {
        if (FAILED(composition_->CreateTargetForHwnd(hwnd_, TRUE, &compositionTarget_)) ||
            FAILED(composition_->CreateVisual(&compositionVisual_))) {
            SafeRelease(compositionTarget_);
            SafeRelease(compositionVisual_);
            SafeRelease(composition_);
        }
    }
    return true;
}

bool Canvas::createSwapChain() {
    if (swapChain_) return true;
    if (!dxgiDevice_ || pixelWidth_ == 0 || pixelHeight_ == 0) return false;

    IDXGIAdapter* adapter = nullptr;
    if (FAILED(dxgiDevice_->GetAdapter(&adapter)) || !adapter) return false;

    IDXGIFactory2* dxgiFactory = nullptr;
    HRESULT hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory));
    adapter->Release();
    if (FAILED(hr) || !dxgiFactory) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = pixelWidth_;
    desc.Height = pixelHeight_;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = 0;

    hr = dxgiFactory->CreateSwapChainForComposition(dxgiDevice_, &desc, nullptr, &swapChain_);
    dxgiFactory->Release();
    if (FAILED(hr) || !swapChain_) {
        swapChain_ = nullptr;
        return false;
    }

    if (composition_ && compositionVisual_) {
        if (FAILED(compositionVisual_->SetContent(swapChain_))) return false;
        composition_->Commit();
    }
    return createTargetBitmap();
}

bool Canvas::createTargetBitmap() {
    if (!target_ || !swapChain_) return false;
    SafeRelease(targetBitmap_);

    IDXGISurface* surface = nullptr;
    HRESULT hr = swapChain_->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(&surface));
    if (FAILED(hr) || !surface) return false;

    D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi_, dpi_);
    hr = target_->CreateBitmapFromDxgiSurface(surface, &properties, &targetBitmap_);
    surface->Release();
    if (FAILED(hr) || !targetBitmap_) return false;

    target_->SetTarget(targetBitmap_);
    target_->SetDpi(dpi_, dpi_);
    return true;
}

void Canvas::releaseTargets() {
    if (target_) target_->SetTarget(nullptr);
    SafeRelease(targetBitmap_);
}

void Canvas::discard() {
    releaseBrushes();
    releaseTargets();
    SafeRelease(swapChain_);
    SafeRelease(target_);
    SafeRelease(d2dDevice_);
    SafeRelease(dxgiDevice_);
    SafeRelease(d3d_);
}

void Canvas::releaseBrushes() {
    for (auto& kv : brushes_) kv.second->Release();
    brushes_.clear();
}

bool Canvas::resize(UINT pixelWidth, UINT pixelHeight, float dpi) {
    float newDpi = dpi > 0 ? dpi : 96.0f;
    bool dpiChanged = newDpi != dpi_;
    dpi_ = newDpi;

    if (pixelWidth == 0 || pixelHeight == 0) {
        pixelWidth_ = pixelWidth;
        pixelHeight_ = pixelHeight;
        return true;
    }

    if (!target_) {
        pixelWidth_ = pixelWidth;
        pixelHeight_ = pixelHeight;
        if (!createDeviceResources()) return false;
        return createSwapChain();
    }

    if (dpiChanged) {
        releaseBrushes();
        if (!createTargetBitmap()) return false;
    }

    if (!swapChain_) {
        pixelWidth_ = pixelWidth;
        pixelHeight_ = pixelHeight;
        return createSwapChain();
    }

    if (pixelWidth == pixelWidth_ && pixelHeight == pixelHeight_ && !dpiChanged) return true;

    releaseTargets();
    HRESULT hr = swapChain_->ResizeBuffers(2, pixelWidth, pixelHeight, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    if (FAILED(hr)) {
        SafeRelease(swapChain_);
        pixelWidth_ = pixelWidth;
        pixelHeight_ = pixelHeight;
        return createSwapChain();
    }
    pixelWidth_ = pixelWidth;
    pixelHeight_ = pixelHeight;
    if (!createTargetBitmap()) return false;
    if (composition_) composition_->Commit();
    return true;
}

bool Canvas::begin() {
    if (!target_ && !createDeviceResources()) return false;
    if (!target_) return false;
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}

bool Canvas::end() {
    if (!target_) return false;
    HRESULT hr = target_->EndDraw();
    if (FAILED(hr)) {
        if (hr == D2DERR_RECREATE_TARGET) {
            UINT w = pixelWidth_, h = pixelHeight_;
            discard();
            pixelWidth_ = w;
            pixelHeight_ = h;
        }
        return false;
    }
    if (!swapChain_) return true;
    hr = swapChain_->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        UINT w = pixelWidth_, h = pixelHeight_;
        discard();
        pixelWidth_ = w;
        pixelHeight_ = h;
        return false;
    }
    return SUCCEEDED(hr);
}

void Canvas::clear(Color color) {
    if (target_) target_->Clear(ToColor(color));
}

ID2D1SolidColorBrush* Canvas::brushFor(Color color) {
    auto it = brushes_.find(color);
    if (it != brushes_.end()) return it->second;
    if (!target_) return nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    if (FAILED(target_->CreateSolidColorBrush(ToColor(color), &brush)) || !brush) return nullptr;
    brushes_[color] = brush;
    return brush;
}

void Canvas::fillRect(const Rect& rect, Color color) {
    if (!target_ || rect.empty()) return;
    ID2D1SolidColorBrush* brush = brushFor(color);
    if (brush) target_->FillRectangle(ToD2D(rect), brush);
}

void Canvas::fillRound(const Rect& rect, float radius, Color color) {
    if (!target_ || rect.empty()) return;
    ID2D1SolidColorBrush* brush = brushFor(color);
    if (!brush) return;
    float r = std::min(radius, std::min(rect.w, rect.h) * 0.5f);
    target_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(rect), r, r), brush);
}

void Canvas::strokeRound(const Rect& rect, float radius, Color color, float width) {
    if (!target_ || rect.empty()) return;
    ID2D1SolidColorBrush* brush = brushFor(color);
    if (!brush) return;
    float r = std::min(radius, std::min(rect.w, rect.h) * 0.5f);
    target_->DrawRoundedRectangle(D2D1::RoundedRect(ToD2D(rect), r, r), brush, width);
}

void Canvas::line(float x1, float y1, float x2, float y2, Color color, float width) {
    if (!target_) return;
    ID2D1SolidColorBrush* brush = brushFor(color);
    if (!brush) return;
    target_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, width);
}

void Canvas::pushClip(const Rect& rect) {
    if (!target_) return;
    target_->PushAxisAlignedClip(ToD2D(rect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ++clipDepth_;
}

void Canvas::popClip() {
    if (!target_ || clipDepth_ <= 0) return;
    target_->PopAxisAlignedClip();
    --clipDepth_;
}

IDWriteTextFormat* Canvas::formatFor(float size, DWRITE_FONT_WEIGHT weight,
                                     DWRITE_TEXT_ALIGNMENT align,
                                     DWRITE_PARAGRAPH_ALIGNMENT valign, bool icons) {
    if (!dwrite_) return nullptr;
    FormatKey key{(int)(size * 100.0f + 0.5f), (int)weight, (int)align, (int)valign, icons};
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second;
    IDWriteTextFormat* format = nullptr;
    const wchar_t* family = icons ? iconFamily_.c_str()
                                  : (size >= 24.0f ? displayFamily_.c_str() : textFamily_.c_str());
    HRESULT hr = dwrite_->CreateTextFormat(family, nullptr,
                                           weight, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &format);
    if (FAILED(hr) || !format) return nullptr;
    format->SetTextAlignment(align);
    format->SetParagraphAlignment(valign);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    formats_[key] = format;
    return format;
}

IDWriteInlineObject* Canvas::ellipsisSign(IDWriteTextFormat* format) {
    if (ellipsis_) return ellipsis_;
    if (dwrite_) dwrite_->CreateEllipsisTrimmingSign(format, &ellipsis_);
    return ellipsis_;
}

void Canvas::text(const Rect& rect, const std::wstring& value, float size, Color color,
                  DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align,
                  DWRITE_PARAGRAPH_ALIGNMENT valign, bool ellipsis) {
    if (!target_ || value.empty() || rect.empty()) return;
    IDWriteTextFormat* format = formatFor(size, weight, align, valign, false);
    ID2D1SolidColorBrush* brush = brushFor(color);
    if (!format || !brush) return;

    if (!ellipsis) {
        target_->DrawTextW(value.c_str(), (UINT32)value.size(), format, ToD2D(rect), brush,
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
        return;
    }

    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = dwrite_->CreateTextLayout(value.c_str(), (UINT32)value.size(), format, rect.w,
                                           rect.h, &layout);
    if (FAILED(hr) || !layout) return;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetParagraphAlignment(valign);
    IDWriteInlineObject* sign = ellipsisSign(format);
    if (sign) {
        DWRITE_TRIMMING trimming{};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        layout->SetTrimming(&trimming, sign);
    }
    target_->DrawTextLayout(D2D1::Point2F(rect.x, rect.y), layout, brush,
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    layout->Release();
}

void Canvas::glyph(const Rect& rect, wchar_t codepoint, float size, Color color,
                   DWRITE_TEXT_ALIGNMENT align) {
    if (!target_ || rect.empty()) return;
    IDWriteTextFormat* format =
        formatFor(size, DWRITE_FONT_WEIGHT_NORMAL, align, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    ID2D1SolidColorBrush* brush = brushFor(color);
    if (!format || !brush) return;
    wchar_t text[2] = {codepoint, 0};
    target_->DrawTextW(text, 1, format, ToD2D(rect), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Canvas::textWidth(const std::wstring& value, float size, DWRITE_FONT_WEIGHT weight) {
    if (value.empty() || !dwrite_) return 0.0f;
    IDWriteTextFormat* format = formatFor(size, weight, DWRITE_TEXT_ALIGNMENT_LEADING,
                                          DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    if (!format) return 0.0f;
    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = dwrite_->CreateTextLayout(value.c_str(), (UINT32)value.size(), format, 8192.0f,
                                           100.0f, &layout);
    if (FAILED(hr) || !layout) return 0.0f;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_METRICS metrics{};
    float width = 0.0f;
    if (SUCCEEDED(layout->GetMetrics(&metrics))) width = metrics.widthIncludingTrailingWhitespace;
    layout->Release();
    return width;
}
