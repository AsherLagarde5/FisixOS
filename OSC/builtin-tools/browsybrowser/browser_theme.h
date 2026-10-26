#pragma once
#include <windows.h>
#include <string>

namespace theme {

// Rich Obsidian & Cyberpunk Dark Palette with Vibrant Accents
constexpr COLORREF BG_TITLEBAR     = RGB(10, 14, 22);     // #0A0E16
constexpr COLORREF BG_TOOLBAR      = RGB(18, 24, 38);     // #121826
constexpr COLORREF BG_BOOKMARKS    = RGB(14, 18, 28);     // #0E121C
constexpr COLORREF BG_TAB_ACTIVE   = RGB(28, 36, 56);     // #1C2438
constexpr COLORREF BG_TAB_INACTIVE = RGB(10, 14, 22);     // #0A0E16
constexpr COLORREF BG_TAB_HOVER    = RGB(38, 48, 72);     // #263048
constexpr COLORREF BG_OMNIBOX      = RGB(10, 14, 22);     // #0A0E16
constexpr COLORREF BG_BTN_HOVER    = RGB(48, 62, 92);     // #303E5C
constexpr COLORREF BG_BTN_ACTIVE   = RGB(68, 86, 126);    // #44567E
constexpr COLORREF BG_CLOSE_HOVER  = RGB(239, 68, 68);    // #EF4444
constexpr COLORREF BG_FIND_BAR     = RGB(18, 24, 38);     // #121826

constexpr COLORREF BORDER_DARK     = RGB(42, 54, 82);     // #2A3652
constexpr COLORREF BORDER_ACCENT   = RGB(0, 240, 192);    // #00F0C0

// Vibrant Colors
constexpr COLORREF ACCENT_TEAL     = RGB(0, 240, 192);    // #00F0C0
constexpr COLORREF ACCENT_CYAN     = RGB(6, 182, 212);    // #06B6D4
constexpr COLORREF ACCENT_PURPLE   = RGB(168, 85, 247);   // #A855F7
constexpr COLORREF ACCENT_PINK     = RGB(236, 72, 153);   // #EC4899
constexpr COLORREF ACCENT_ORANGE   = RGB(249, 115, 22);   // #F97316
constexpr COLORREF ACCENT_GOLD     = RGB(250, 204, 21);   // #FACC15
constexpr COLORREF ACCENT_GREEN    = RGB(16, 185, 129);   // #10B981
constexpr COLORREF ACCENT_RED      = RGB(239, 68, 68);    // #EF4444
constexpr COLORREF ACCENT_BLUE     = RGB(59, 130, 246);   // #3B82F6

constexpr COLORREF TEXT_PRIMARY    = RGB(248, 250, 252);  // #F8FAFC
constexpr COLORREF TEXT_MUTED      = RGB(148, 163, 184);  // #94A3B8
constexpr COLORREF TEXT_DISABLED   = RGB(71, 85, 105);    // #475569
constexpr COLORREF TEXT_ACCENT     = RGB(0, 240, 192);    // #00F0C0

struct Fonts {
    HFONT ui = nullptr;
    HFONT ui_bold = nullptr;
    HFONT tab_font = nullptr;
    HFONT icon_font = nullptr;
    HFONT small_font = nullptr;
    HFONT caption_font = nullptr;

    void init() {
        cleanup();
        ui = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        ui_bold = CreateFontW(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        tab_font = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        icon_font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
        small_font = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        caption_font = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    void cleanup() {
        if (ui) { DeleteObject(ui); ui = nullptr; }
        if (ui_bold) { DeleteObject(ui_bold); ui_bold = nullptr; }
        if (tab_font) { DeleteObject(tab_font); tab_font = nullptr; }
        if (icon_font) { DeleteObject(icon_font); icon_font = nullptr; }
        if (small_font) { DeleteObject(small_font); small_font = nullptr; }
        if (caption_font) { DeleteObject(caption_font); caption_font = nullptr; }
    }
};

inline void fill_rect(HDC hdc, const RECT& r, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    FillRect(hdc, &r, br);
    DeleteObject(br);
}

inline void draw_rounded_rect(HDC hdc, const RECT& r, int radius, COLORREF fill, COLORREF border, int border_width = 1) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = (border_width > 0) ? CreatePen(PS_SOLID, border_width, border) : (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ old_br = SelectObject(hdc, br);
    HGDIOBJ old_pen = SelectObject(hdc, pen);

    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius, radius);

    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_br);
    if (border_width > 0) DeleteObject(pen);
    DeleteObject(br);
}

inline void draw_text_centered(HDC hdc, const std::wstring& text, const RECT& r, COLORREF color, HFONT font) {
    HGDIOBJ old_font = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    RECT rect = r;
    DrawTextW(hdc, text.c_str(), (int)text.length(), &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, old_font);
}

inline void draw_text_left(HDC hdc, const std::wstring& text, const RECT& r, COLORREF color, HFONT font, bool ellipsis = false) {
    HGDIOBJ old_font = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    RECT rect = r;
    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
    if (ellipsis) flags |= DT_END_ELLIPSIS;
    DrawTextW(hdc, text.c_str(), (int)text.length(), &rect, flags);
    SelectObject(hdc, old_font);
}

} // namespace theme
