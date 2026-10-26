#pragma once
#include "engine_layout.h"
#include "engine_paint.h"
#include "engine_network.h"
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <map>
#include <memory>
#include <sstream>

#pragma comment(lib, "gdiplus.lib")

// -----------------------------------------------------------------------------
// engine_renderer.h  —  Go render pipeline bridge
//
// The render pipeline now lives in Go (render_pipeline/render_pipeline.dll).
// This file:
//   1. Serializes the C++ DisplayList to JSON (via engine_paint.h)
//   2. Dynamically loads render_pipeline.dll on first call
//   3. Calls go_render(hdc, json_ptr, json_len) across the FFI
//
// Go side (render_pipeline/):
//   compositor.go      — z-sort layer tree, back-to-front paint
//   canvas_gdiplus.go  — CGo GDI+ rasterizer
//   go_renderer.go     — exported C entry points
// -----------------------------------------------------------------------------

namespace engine {

// ---------------------------------------------------------------------------
// ImageCache — kept here for internal pages / legacy callers that still
// need synchronous image fetching via GDI+.
// ---------------------------------------------------------------------------
class ImageCache {
public:
    static ImageCache& instance() {
        static ImageCache s;
        return s;
    }
    Gdiplus::Image* get_image(const std::wstring& url) {
        if (url.empty()) return nullptr;
        auto it = cache_.find(url);
        if (it != cache_.end()) return it->second.get();
        auto resp = NetworkClient::fetch(url);
        if (!resp.success || resp.body_bytes.empty()) return nullptr;
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, resp.body_bytes.size());
        if (!hg) return nullptr;
        memcpy(GlobalLock(hg), resp.body_bytes.data(), resp.body_bytes.size());
        GlobalUnlock(hg);
        IStream* stream = nullptr;
        if (!SUCCEEDED(CreateStreamOnHGlobal(hg, TRUE, &stream)) || !stream) return nullptr;
        auto img = std::unique_ptr<Gdiplus::Image>(Gdiplus::Image::FromStream(stream));
        stream->Release();
        if (!img || img->GetLastStatus() != Gdiplus::Ok) return nullptr;
        Gdiplus::Image* raw = img.get();
        cache_[url] = std::move(img);
        return raw;
    }
private:
    std::map<std::wstring, std::unique_ptr<Gdiplus::Image>> cache_;
};

// ---------------------------------------------------------------------------
// GoRenderBridge — lazy-loads render_pipeline.dll and calls go_render().
// ---------------------------------------------------------------------------

class GoRenderBridge {
public:
    // Returns 1 on success, 0 on failure (malformed JSON, empty frame, etc.)
    using GoRenderFn = int(*)(uintptr_t hdc, const char* json, int json_len);

    static GoRenderBridge& instance() {
        static GoRenderBridge s;
        return s;
    }

    bool call(HDC hdc, const std::string& json) {
        if (!ensure_loaded() || json.empty()) return false;
        return render_fn_(reinterpret_cast<uintptr_t>(hdc), json.c_str(), static_cast<int>(json.size())) != 0;
    }

    bool loaded() const { return dll_ != nullptr && render_fn_ != nullptr; }

private:
    HMODULE dll_ = nullptr;
    GoRenderFn render_fn_ = nullptr;

    bool ensure_loaded() {
        if (loaded()) return true;
        // Look for the DLL next to the executable, then in the project root
        dll_ = LoadLibraryW(L"render_pipeline.dll");
        if (!dll_) {
            // Try relative path from project root (dev builds)
            dll_ = LoadLibraryW(L"render_pipeline\\render_pipeline.dll");
        }
        if (!dll_) return false;
        render_fn_ = reinterpret_cast<GoRenderFn>(GetProcAddress(dll_, "go_render"));
        return render_fn_ != nullptr;
    }
};

// ---------------------------------------------------------------------------
// JSON serializer for DisplayList → Go
// Produces the RenderRequest JSON payload consumed by go_renderer.go.
// ---------------------------------------------------------------------------

namespace detail {

inline std::string color_json(const Color& c) {
    char buf[64];
    snprintf(buf, sizeof(buf), R"({"R":%d,"G":%d,"B":%d,"A":%d})", c.r, c.g, c.b, c.a);
    return buf;
}

inline std::string rect_json(int x, int y, int w, int h) {
    char buf[64];
    snprintf(buf, sizeof(buf), R"({"X":%d,"Y":%d,"Width":%d,"Height":%d})", x, y, w, h);
    return buf;
}

inline std::string rect_json(const Rect& r) {
    return rect_json(r.x, r.y, r.width, r.height);
}

inline std::string escape_json(const std::wstring& ws) {
    std::string out = "\"";
    for (wchar_t wc : ws) {
        if (wc == L'"')       out += "\\\"";
        else if (wc == L'\\') out += "\\\\";
        else if (wc < 0x80)   out += static_cast<char>(wc);
        else {
            char esc[16];
            snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned>(wc));
            out += esc;
        }
    }
    out += '"';
    return out;
}

inline std::string style_json(const ComputedStyle& s) {
    std::string fam = escape_json(s.font_family.empty() ? L"Segoe UI" : s.font_family);
    char buf[256];
    snprintf(buf, sizeof(buf),
        R"({"font_family":%s,"font_size":%d,"font_weight":%d,"is_italic":%s,"is_underline":%s,"padding_top":%d,"padding_left":%d})",
        fam.c_str(), s.font_size > 0 ? s.font_size : 13, s.font_weight,
        s.is_italic ? "true" : "false", s.is_underline ? "true" : "false",
        s.padding.top, s.padding.left);
    return buf;
}

// Serialize a single PaintCommand to JSON
inline std::string cmd_json(const paint::PaintCommand& cmd) {
    return std::visit([](const auto& c) -> std::string {
        using T = std::decay_t<decltype(c)>;

        if constexpr (std::is_same_v<T, paint::CmdSaveLayer>) {
            char buf[64]; snprintf(buf, sizeof(buf), R"({"type":"save_layer","opacity":%.3f})", c.opacity);
            return buf;
        } else if constexpr (std::is_same_v<T, paint::CmdRestore>) {
            return R"({"type":"restore"})";
        } else if constexpr (std::is_same_v<T, paint::CmdClipRect>) {
            return R"({"type":"clip_rect","bounds":)" + rect_json(c.clip) + "}";
        } else if constexpr (std::is_same_v<T, paint::CmdFillRect>) {
            return R"({"type":"fill_rect","bounds":)" + rect_json(c.bounds) + R"(,"color":)" + color_json(c.color) + "}";
        } else if constexpr (std::is_same_v<T, paint::CmdFillRoundedRect>) {
            char buf[32]; snprintf(buf, sizeof(buf), R"(,"radius":%d})", c.radius);
            return R"({"type":"fill_rounded_rect","bounds":)" + rect_json(c.bounds) + R"(,"color":)" + color_json(c.color) + buf;
        } else if constexpr (std::is_same_v<T, paint::CmdStrokeRoundedRect>) {
            char buf[64]; snprintf(buf, sizeof(buf), R"(,"radius":%d,"stroke_width":%d})", c.radius, c.stroke_width);
            return R"({"type":"stroke_rounded_rect","bounds":)" + rect_json(c.bounds) + R"(,"color":)" + color_json(c.color) + buf;
        } else if constexpr (std::is_same_v<T, paint::CmdStrokeRect>) {
            char buf[32]; snprintf(buf, sizeof(buf), R"(,"stroke_width":%d})", c.stroke_width);
            return R"({"type":"stroke_rect","bounds":)" + rect_json(c.bounds) + R"(,"color":)" + color_json(c.color) + buf;
        } else if constexpr (std::is_same_v<T, paint::CmdDrawText>) {
            return R"({"type":"draw_text","text":)" + escape_json(c.text) +
                   R"(,"bounds":)" + rect_json(c.bounds) +
                   R"(,"style":)" + style_json(c.style) +
                   R"(,"color":)" + color_json(c.color) + "}";
        } else if constexpr (std::is_same_v<T, paint::CmdDrawImage>) {
            return R"({"type":"draw_image","url":)" + escape_json(c.url) +
                   R"(,"bounds":)" + rect_json(c.bounds) + "}";
        } else if constexpr (std::is_same_v<T, paint::CmdDrawButton>) {
            return R"({"type":"draw_button","text":)" + escape_json(c.label) +
                   R"(,"bounds":)" + rect_json(c.bounds) + "}";
        } else if constexpr (std::is_same_v<T, paint::CmdDrawInput>) {
            return R"({"type":"draw_input","value":)" + escape_json(c.value) +
                   R"(,"placeholder":)" + escape_json(c.placeholder) +
                   R"(,"bounds":)" + rect_json(c.bounds) + "}";
        }
        return "{}";
    }, cmd);
}

// Serialize the full layer list to a RenderRequest JSON string
inline std::string build_render_json(
    const LayoutBox* root,
    int scroll_y,
    const RECT& viewport,
    const std::wstring& hovered_href
) {
    using namespace paint;

    // Build display list via existing C++ DisplayListBuilder
    DisplayList dl = DisplayListBuilder::build(root, scroll_y, viewport, hovered_href);

    // Single root layer (z-index 0, opacity 1.0)
    std::string cmds_json;
    bool first = true;
    for (const auto& cmd : dl.commands()) {
        if (!first) cmds_json += ',';
        cmds_json += cmd_json(cmd);
        first = false;
    }

    return R"({"layers":[{"z_index":0,"opacity":1.0,"display_list":{"commands":[)" +
           cmds_json + R"(]}}]})";
}

} // namespace detail

// ---------------------------------------------------------------------------
// GDI+ fallback painter (used when Go DLL is unavailable)
// Identical to the old Renderer so pages still display if the DLL is missing.
// ---------------------------------------------------------------------------

class FallbackRenderer {
public:
    static void init_gdiplus() {
        static ULONG_PTR token = 0;
        if (token == 0) {
            Gdiplus::GdiplusStartupInput in;
            Gdiplus::GdiplusStartup(&token, &in, nullptr);
        }
    }

    static void paint(HDC hdc, const LayoutBox* root, int scroll_y,
                      const RECT& viewport, const std::wstring& hovered_href) {
        init_gdiplus();
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        paint_box(hdc, g, root, scroll_y, viewport, hovered_href);
    }

private:
    static Gdiplus::Color to_gdi(const Color& c) { return Gdiplus::Color(c.a,c.r,c.g,c.b); }

    static void rounded_path(Gdiplus::Graphics& g, int x,int y,int w,int h,int r,
                              const Gdiplus::Brush* br, const Gdiplus::Pen* pen) {
        if (w<=0||h<=0) return;
        if (r*2>w) r=w/2; if (r*2>h) r=h/2;
        if (r<=0) { if(br) g.FillRectangle(br,x,y,w,h); if(pen) g.DrawRectangle(pen,x,y,w,h); return; }
        int d=r*2; Gdiplus::GraphicsPath p;
        p.AddArc(x,y,d,d,180,90); p.AddArc(x+w-d,y,d,d,270,90);
        p.AddArc(x+w-d,y+h-d,d,d,0,90); p.AddArc(x,y+h-d,d,d,90,90); p.CloseFigure();
        if(br) g.FillPath(br,&p); if(pen) g.DrawPath(pen,&p);
    }

    static void paint_box(HDC hdc, Gdiplus::Graphics& g, const LayoutBox* box,
                           int scroll_y, const RECT& vp, const std::wstring& hover) {
        if (!box || box->display == Display::None) return;
        int sy = box->bounds.y - scroll_y + vp.top, sx = box->bounds.x + vp.left;
        int bw = box->bounds.width, bh = box->bounds.height;
        if (sy+bh < vp.top || sy > vp.bottom) return;

        if (!box->style.background_color.is_transparent()) {
            Gdiplus::SolidBrush br(to_gdi(box->style.background_color));
            rounded_path(g,sx,sy,bw,bh,box->style.border_radius,&br,nullptr);
        }
        if (box->style.border_width>0 && !box->style.border_color.is_transparent()) {
            Gdiplus::Pen pen(to_gdi(box->style.border_color),(Gdiplus::REAL)box->style.border_width);
            rounded_path(g,sx,sy,bw,bh,box->style.border_radius,nullptr,&pen);
        }
        if (box->is_image && !box->image_src.empty()) {
            auto* img = ImageCache::instance().get_image(box->image_src);
            if (img) g.DrawImage(img,sx,sy,bw,bh);
        }
        for (const auto& line : box->lines) {
            for (const auto& item : line.items) {
                int ix=item.bounds.x+vp.left, iy=item.bounds.y-scroll_y+vp.top;
                if (iy+item.bounds.height<vp.top||iy>vp.bottom) continue;
                if (item.is_image && !item.image_src.empty()) {
                    auto* img = ImageCache::instance().get_image(item.image_src);
                    if (img) g.DrawImage(img,ix,iy,item.bounds.width,item.bounds.height);
                    continue;
                }
                if (item.is_button) {
                    Gdiplus::SolidBrush bg(Gdiplus::Color(255,33,38,45));
                    Gdiplus::Pen bp(Gdiplus::Color(255,48,54,61),1);
                    rounded_path(g,ix,iy,item.bounds.width,item.bounds.height,6,&bg,&bp);
                    Gdiplus::SolidBrush tb(Gdiplus::Color(255,240,246,252));
                    Gdiplus::Font f(L"Segoe UI",13,Gdiplus::FontStyleBold);
                    Gdiplus::StringFormat sf; sf.SetAlignment(Gdiplus::StringAlignmentCenter); sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    Gdiplus::RectF rf((Gdiplus::REAL)ix,(Gdiplus::REAL)iy,(Gdiplus::REAL)item.bounds.width,(Gdiplus::REAL)item.bounds.height);
                    g.DrawString(item.text.c_str(),-1,&f,rf,&sf,&tb); continue;
                }
                if (item.is_input) {
                    Gdiplus::SolidBrush bg(Gdiplus::Color(255,22,27,34));
                    Gdiplus::Pen bp(Gdiplus::Color(255,48,54,61),1);
                    rounded_path(g,ix,iy,item.bounds.width,item.bounds.height,6,&bg,&bp);
                    const std::wstring& v = !item.input_value.empty()?item.input_value:item.input_placeholder;
                    Gdiplus::SolidBrush tb(item.input_value.empty()?Gdiplus::Color(255,120,130,140):Gdiplus::Color(255,240,246,252));
                    Gdiplus::Font f(L"Segoe UI",13); Gdiplus::PointF pt((Gdiplus::REAL)(ix+8),(Gdiplus::REAL)(iy+6));
                    g.DrawString(v.c_str(),-1,&f,pt,&tb); continue;
                }
                Color wc = item.style.color;
                if (item.is_link && !hover.empty() && hover==item.href) wc=Color(255,220,100);
                INT fs=Gdiplus::FontStyleRegular;
                if(item.style.font_weight==FW_BOLD) fs|=Gdiplus::FontStyleBold;
                if(item.style.is_italic) fs|=Gdiplus::FontStyleItalic;
                if(item.style.is_underline) fs|=Gdiplus::FontStyleUnderline;
                Gdiplus::Font f(item.style.font_family.empty()?L"Segoe UI":item.style.font_family.c_str(),(Gdiplus::REAL)item.style.font_size,fs);
                Gdiplus::SolidBrush tb(to_gdi(wc));
                Gdiplus::PointF pt((Gdiplus::REAL)(ix+item.style.padding.left),(Gdiplus::REAL)(iy+item.style.padding.top));
                g.DrawString(item.text.c_str(),-1,&f,pt,&tb);
            }
        }
        for (const auto& child : box->children) {
            if (child->display == Display::None) continue;
            // Block containers paint nested layout; inline runs are flattened into lines.
            if (child->display == Display::Block || child->display == Display::InlineBlock)
                paint_box(hdc, g, child.get(), scroll_y, vp, hover);
        }
    }
};

// ---------------------------------------------------------------------------
// Renderer — public API (unchanged signature)
//
// Tries Go pipeline first; falls back to GDI+ if DLL not available.
// ---------------------------------------------------------------------------

class Renderer {
public:
    static void init_gdiplus() { FallbackRenderer::init_gdiplus(); }

    static void paint_layout_tree(
        HDC hdc,
        const LayoutBox* root_box,
        int scroll_y,
        const RECT& viewport_bounds,
        const std::wstring& hovered_href
    ) {
        if (!root_box) return;

        auto& bridge = GoRenderBridge::instance();
        std::string json = detail::build_render_json(root_box, scroll_y, viewport_bounds, hovered_href);
        if (bridge.call(hdc, json)) return;

        // Fallback: C++ GDI+ direct paint
        FallbackRenderer::paint(hdc, root_box, scroll_y, viewport_bounds, hovered_href);
    }
};

} // namespace engine
