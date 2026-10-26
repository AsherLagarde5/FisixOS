#pragma once
#include "engine_paint.h"
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <stack>

#pragma comment(lib, "gdiplus.lib")

// -----------------------------------------------------------------------------
// engine_canvas.h
//
// Stage 3 of the Chrome-style render pipeline:
//   Display List → Compositing → [RASTERIZE: Canvas → pixels]
//
// Canvas is a thin abstraction over Gdiplus::Graphics that mirrors the
// Skia SkCanvas API surface. This makes it a 1-file swap to plug in:
//   - Real Skia (SkCanvas) for GPU-accelerated rendering
//   - Direct2D (ID2D1RenderTarget) for hardware acceleration on Windows
//
// Current backend: GDI+ (software, zero external deps)
// -----------------------------------------------------------------------------

namespace engine {

// ---------------------------------------------------------------------------
// GDI+ startup guard (RAII singleton)
// ---------------------------------------------------------------------------
class GdiplusGuard {
public:
    static void ensure_initialized() {
        static GdiplusGuard s_guard;
    }
private:
    ULONG_PTR token_ = 0;
    GdiplusGuard() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token_, &input, nullptr);
    }
    ~GdiplusGuard() {
        if (token_) Gdiplus::GdiplusShutdown(token_);
    }
};

// ---------------------------------------------------------------------------
// Canvas — Skia-like draw API backed by GDI+
//
// Interface mirrors SkCanvas so you can swap backends without touching
// the compositor or display-list replayer.
//
// Key operations:
//   save() / restore()       — push/pop the graphics state stack
//   clipRect()               — intersect clip region
//   drawRect()               — filled rectangle
//   drawRoundedRect()        — filled rounded rectangle
//   strokeRect()             — stroked rectangle
//   strokeRoundedRect()      — stroked rounded rect
//   drawText()               — text with ComputedStyle
//   drawImage()              — bitmap from ImageCache
//   drawButton()             — styled form button widget
//   drawInput()              — styled text input widget
// ---------------------------------------------------------------------------

class Canvas {
public:
    // Construct from a raw HDC (window, bitmap, offscreen surface, etc.)
    explicit Canvas(HDC hdc) : hdc_(hdc) {
        GdiplusGuard::ensure_initialized();
        graphics_ = std::make_unique<Gdiplus::Graphics>(hdc);
        graphics_->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics_->SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        graphics_->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    }

    ~Canvas() = default;

    // Non-copyable, movable
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    // -----------------------------------------------------------------------
    // State stack (mirrors SkCanvas::save / restore)
    // -----------------------------------------------------------------------

    void save() {
        save_stack_.push(graphics_->Save());
    }

    void restore() {
        if (!save_stack_.empty()) {
            graphics_->Restore(save_stack_.top());
            save_stack_.pop();
        }
    }

    void save_layer(float opacity) {
        save();
        // Apply opacity via a ColorMatrix attribute on the graphics state
        Gdiplus::ColorMatrix cm = {
            1,0,0,0,0,
            0,1,0,0,0,
            0,0,1,0,0,
            0,0,0,opacity,0,
            0,0,0,0,1
        };
        Gdiplus::ImageAttributes attrs;
        attrs.SetColorMatrix(&cm, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
        // Store for use during drawImage inside this layer
        layer_opacity_stack_.push(opacity);
    }

    void restore_layer() {
        if (!layer_opacity_stack_.empty()) layer_opacity_stack_.pop();
        restore();
    }

    void clip_rect(const Rect& r) {
        graphics_->SetClip(Gdiplus::Rect(r.x, r.y, r.width, r.height), Gdiplus::CombineModeIntersect);
    }

    // -----------------------------------------------------------------------
    // Fill primitives
    // -----------------------------------------------------------------------

    void draw_rect(const Rect& r, const Color& c) {
        if (r.width <= 0 || r.height <= 0 || c.is_transparent()) return;
        Gdiplus::SolidBrush br(to_gdi(c));
        graphics_->FillRectangle(&br, r.x, r.y, r.width, r.height);
    }

    void draw_rounded_rect(const Rect& r, const Color& c, int radius) {
        if (r.width <= 0 || r.height <= 0 || c.is_transparent()) return;
        Gdiplus::SolidBrush br(to_gdi(c));
        fill_rounded_rect_path(*graphics_, r.x, r.y, r.width, r.height, radius, &br, nullptr);
    }

    // -----------------------------------------------------------------------
    // Stroke primitives
    // -----------------------------------------------------------------------

    void stroke_rect(const Rect& r, const Color& c, int width) {
        if (r.width <= 0 || r.height <= 0 || c.is_transparent() || width <= 0) return;
        Gdiplus::Pen pen(to_gdi(c), static_cast<Gdiplus::REAL>(width));
        graphics_->DrawRectangle(&pen, r.x, r.y, r.width, r.height);
    }

    void stroke_rounded_rect(const Rect& r, const Color& c, int radius, int width) {
        if (r.width <= 0 || r.height <= 0 || c.is_transparent() || width <= 0) return;
        Gdiplus::Pen pen(to_gdi(c), static_cast<Gdiplus::REAL>(width));
        fill_rounded_rect_path(*graphics_, r.x, r.y, r.width, r.height, radius, nullptr, &pen);
    }

    // -----------------------------------------------------------------------
    // Text
    // -----------------------------------------------------------------------

    void draw_text(
        const std::wstring& text,
        const Rect& bounds,
        const ComputedStyle& style,
        const Color& color
    ) {
        if (text.empty()) return;

        INT gdi_style = Gdiplus::FontStyleRegular;
        if (style.font_weight == FW_BOLD) gdi_style |= Gdiplus::FontStyleBold;
        if (style.is_italic)              gdi_style |= Gdiplus::FontStyleItalic;
        if (style.is_underline)           gdi_style |= Gdiplus::FontStyleUnderline;

        const auto& family = style.font_family.empty() ? L"Segoe UI" : style.font_family.c_str();
        const float size   = style.font_size > 0 ? static_cast<float>(style.font_size) : 13.0f;

        Gdiplus::Font font(family, size, gdi_style, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush brush(to_gdi(color));

        const int off_x = style.padding.left > 0 ? style.padding.left : 0;
        const int off_y = style.padding.top  > 0 ? style.padding.top  : 0;
        Gdiplus::PointF pt(
            static_cast<Gdiplus::REAL>(bounds.x + off_x),
            static_cast<Gdiplus::REAL>(bounds.y + off_y)
        );
        graphics_->DrawString(text.c_str(), -1, &font, pt, &brush);
    }

    // -----------------------------------------------------------------------
    // Images (fetched via ImageCache)
    // -----------------------------------------------------------------------

    void draw_image(const std::wstring& url, const Rect& bounds) {
        if (url.empty() || bounds.width <= 0 || bounds.height <= 0) return;
        Gdiplus::Image* img = get_cached_image(url);
        if (img) {
            graphics_->DrawImage(img, bounds.x, bounds.y, bounds.width, bounds.height);
        }
    }

    // -----------------------------------------------------------------------
    // Composite widgets — buttons & inputs
    // These match Chrome's form-control rendering style.
    // -----------------------------------------------------------------------

    void draw_button(const std::wstring& label, const Rect& bounds) {
        // Background
        Gdiplus::SolidBrush bg(Gdiplus::Color(255, 33, 38, 45));
        Gdiplus::Pen border(Gdiplus::Color(255, 48, 54, 61), 1);
        fill_rounded_rect_path(*graphics_, bounds.x, bounds.y, bounds.width, bounds.height, 6, &bg, &border);

        // Centered label
        Gdiplus::SolidBrush text_br(Gdiplus::Color(255, 240, 246, 252));
        Gdiplus::Font font(L"Segoe UI", 13, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF rf(
            static_cast<Gdiplus::REAL>(bounds.x),
            static_cast<Gdiplus::REAL>(bounds.y),
            static_cast<Gdiplus::REAL>(bounds.width),
            static_cast<Gdiplus::REAL>(bounds.height)
        );
        graphics_->DrawString(label.c_str(), -1, &font, rf, &fmt, &text_br);
    }

    void draw_input(const std::wstring& value, const std::wstring& placeholder, const Rect& bounds) {
        // Background + border
        Gdiplus::SolidBrush bg(Gdiplus::Color(255, 22, 27, 34));
        Gdiplus::Pen border(Gdiplus::Color(255, 48, 54, 61), 1);
        fill_rounded_rect_path(*graphics_, bounds.x, bounds.y, bounds.width, bounds.height, 6, &bg, &border);

        // Text or placeholder
        const std::wstring& display_text = !value.empty() ? value : placeholder;
        Gdiplus::SolidBrush text_br(
            value.empty()
            ? Gdiplus::Color(255, 120, 130, 140)  // placeholder grey
            : Gdiplus::Color(255, 240, 246, 252)  // input text white
        );
        Gdiplus::Font font(L"Segoe UI", 13, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::PointF pt(
            static_cast<Gdiplus::REAL>(bounds.x + 8),
            static_cast<Gdiplus::REAL>(bounds.y + 6)
        );
        graphics_->DrawString(display_text.c_str(), -1, &font, pt, &text_br);
    }

    // -----------------------------------------------------------------------
    // Direct HDC access (for legacy hit-testing / text measurement)
    // -----------------------------------------------------------------------
    HDC hdc() const { return hdc_; }
    Gdiplus::Graphics& gfx() { return *graphics_; }

private:
    HDC hdc_;
    std::unique_ptr<Gdiplus::Graphics> graphics_;
    std::stack<Gdiplus::GraphicsState> save_stack_;
    std::stack<float> layer_opacity_stack_;

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    static Gdiplus::Color to_gdi(const Color& c) {
        return Gdiplus::Color(c.a, c.r, c.g, c.b);
    }

    // Rounded-rect path builder — shared between fill and stroke paths
    static void fill_rounded_rect_path(
        Gdiplus::Graphics& g,
        int x, int y, int w, int h, int radius,
        const Gdiplus::Brush* brush,
        const Gdiplus::Pen* pen
    ) {
        if (w <= 0 || h <= 0) return;
        if (radius * 2 > w) radius = w / 2;
        if (radius * 2 > h) radius = h / 2;

        if (radius <= 0) {
            if (brush) g.FillRectangle(brush, x, y, w, h);
            if (pen)   g.DrawRectangle(pen,   x, y, w, h);
            return;
        }

        const int d = radius * 2;
        Gdiplus::GraphicsPath path;
        path.AddArc(x,         y,         d, d, 180, 90);
        path.AddArc(x + w - d, y,         d, d, 270, 90);
        path.AddArc(x + w - d, y + h - d, d, d,   0, 90);
        path.AddArc(x,         y + h - d, d, d,  90, 90);
        path.CloseFigure();

        if (brush) g.FillPath(brush, &path);
        if (pen)   g.DrawPath(pen,   &path);
    }

    // Forward declaration — implemented in engine_renderer.h to avoid circular deps
    static Gdiplus::Image* get_cached_image(const std::wstring& url);
};

} // namespace engine
