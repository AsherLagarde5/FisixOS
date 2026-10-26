#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstdint>

namespace engine {

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    Color() = default;
    Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}

    COLORREF to_colorref() const {
        return RGB(r, g, b);
    }

    bool is_transparent() const {
        return a == 0;
    }

    static Color transparent() {
        return Color(0, 0, 0, 0);
    }

    static Color from_hex(const std::wstring& hex_str) {
        std::wstring s = hex_str;
        if (!s.empty() && s[0] == L'#') s = s.substr(1);
        if (s.length() == 3 || s.length() == 4) {
            // #RGB -> #RRGGBB
            std::wstring expanded;
            for (wchar_t ch : s) {
                expanded.push_back(ch);
                expanded.push_back(ch);
            }
            s = std::move(expanded);
        }
        if (s.length() == 6 || s.length() == 8) {
            try {
                unsigned long val = std::stoul(s, nullptr, 16);
                if (s.length() == 8) {
                    return Color(
                        static_cast<uint8_t>((val >> 24) & 0xFF),
                        static_cast<uint8_t>((val >> 16) & 0xFF),
                        static_cast<uint8_t>((val >> 8) & 0xFF),
                        static_cast<uint8_t>(val & 0xFF)
                    );
                }
                return Color(
                    static_cast<uint8_t>((val >> 16) & 0xFF),
                    static_cast<uint8_t>((val >> 8) & 0xFF),
                    static_cast<uint8_t>(val & 0xFF)
                );
            } catch (...) {}
        }
        return Color(0, 0, 0, 255);
    }

    static Color from_name(const std::wstring& name) {
        std::wstring n = name;
        std::transform(n.begin(), n.end(), n.begin(), ::towlower);
        if (n == L"white") return Color(255, 255, 255);
        if (n == L"black") return Color(0, 0, 0);
        if (n == L"red") return Color(230, 50, 50);
        if (n == L"green") return Color(46, 160, 67);
        if (n == L"blue") return Color(30, 130, 240);
        if (n == L"yellow") return Color(245, 200, 30);
        if (n == L"purple") return Color(140, 60, 200);
        if (n == L"cyan") return Color(0, 210, 180);
        if (n == L"gray" || n == L"grey") return Color(128, 128, 128);
        if (n == L"lightgray" || n == L"lightgrey") return Color(220, 220, 220);
        if (n == L"darkgray" || n == L"darkgrey") return Color(50, 50, 50);
        if (n == L"orange") return Color(255, 165, 0);
        if (n == L"pink") return Color(255, 192, 203);
        if (n == L"teal") return Color(0, 128, 128);
        if (n == L"navy") return Color(0, 0, 128);
        if (n == L"lime") return Color(0, 255, 0);
        if (n == L"rebeccapurple") return Color(102, 51, 153);
        if (n == L"transparent") return Color::transparent();
        return Color(0, 0, 0);
    }

    static Color parse(const std::wstring& str) {
        std::wstring s = str;
        while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
        while (!s.empty() && iswspace(s.back())) s.pop_back();
        if (s.empty()) return Color(0, 0, 0);
        if (s[0] == L'#') return from_hex(s);
        if (s.rfind(L"rgb", 0) == 0) {
            size_t p1 = s.find(L'(');
            size_t p2 = s.find(L')');
            if (p1 != std::wstring::npos && p2 != std::wstring::npos) {
                std::wstring vals = s.substr(p1 + 1, p2 - p1 - 1);
                std::replace(vals.begin(), vals.end(), L',', L' ');
                std::replace(vals.begin(), vals.end(), L'/', L' ');
                std::wstringstream wss(vals);
                double channels[4] = { 0, 0, 0, 1 };
                size_t count = 0;
                while (count < 4 && wss >> channels[count]) ++count;
                if (count >= 3) {
                    double alpha = channels[3] <= 1.0 ? channels[3] * 255.0 : channels[3];
                    return Color(
                        static_cast<uint8_t>(std::clamp(channels[0], 0.0, 255.0)),
                        static_cast<uint8_t>(std::clamp(channels[1], 0.0, 255.0)),
                        static_cast<uint8_t>(std::clamp(channels[2], 0.0, 255.0)),
                        static_cast<uint8_t>(std::clamp(alpha, 0.0, 255.0))
                    );
                }
            }
        }
        return from_name(s);
    }
};

enum class Display {
    Block,
    Inline,
    InlineBlock,
    None
};

enum class TextAlign {
    Left,
    Center,
    Right
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    int right() const { return x + width; }
    int bottom() const { return y + height; }

    bool contains(int px, int py) const {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }

    RECT to_win32_rect() const {
        return RECT{ x, y, x + width, y + height };
    }
};

struct EdgeInsets {
    int top = 0;
    int right = 0;
    int bottom = 0;
    int left = 0;
};

inline std::wstring trim(const std::wstring& s) {
    size_t start = s.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    size_t end = s.find_last_not_of(L" \t\r\n");
    return s.substr(start, end - start + 1);
}

inline std::wstring to_lower(const std::wstring& s) {
    std::wstring res = s;
    std::transform(res.begin(), res.end(), res.begin(), ::towlower);
    return res;
}

inline std::wstring decode_html_entities(const std::wstring& html) {
    std::wstring out;
    out.reserve(html.size());
    for (size_t i = 0; i < html.size(); ++i) {
        if (html[i] == L'&') {
            size_t semi = html.find(L';', i);
            if (semi != std::wstring::npos && semi - i <= 10) {
                std::wstring ent = html.substr(i, semi - i + 1);
                if (ent == L"&nbsp;") { out += L' '; i = semi; continue; }
                if (ent == L"&amp;") { out += L'&'; i = semi; continue; }
                if (ent == L"&lt;") { out += L'<'; i = semi; continue; }
                if (ent == L"&gt;") { out += L'>'; i = semi; continue; }
                if (ent == L"&quot;") { out += L'\"'; i = semi; continue; }
                if (ent == L"&apos;") { out += L'\''; i = semi; continue; }
                if (ent == L"&copy;") { out += L"\u00A9"; i = semi; continue; }
                if (ent == L"&mdash;") { out += L"\u2014"; i = semi; continue; }
                if (ent == L"&ndash;") { out += L"\u2013"; i = semi; continue; }
                if (ent == L"&bull;") { out += L"\u2022"; i = semi; continue; }
                if (ent.size() > 3 && ent[1] == L'#') {
                    try {
                        int code = (ent[2] == L'x' || ent[2] == L'X')
                            ? std::stoi(ent.substr(3, ent.size() - 4), nullptr, 16)
                            : std::stoi(ent.substr(2, ent.size() - 3), nullptr, 10);
                        out += static_cast<wchar_t>(code);
                        i = semi;
                        continue;
                    } catch (...) {}
                }
            }
        }
        out += html[i];
    }
    return out;
}

inline std::wstring resolve_url(const std::wstring& base_url, const std::wstring& relative_url) {
    if (relative_url.find(L"://") != std::wstring::npos || relative_url.rfind(L"browsy://", 0) == 0 || relative_url.rfind(L"data:", 0) == 0) {
        return relative_url;
    }
    if (relative_url.empty()) return base_url;

    // Extract scheme and host from base_url
    size_t scheme_end = base_url.find(L"://");
    if (scheme_end == std::wstring::npos) return relative_url;
    std::wstring scheme = base_url.substr(0, scheme_end + 3);
    std::wstring rest = base_url.substr(scheme_end + 3);
    size_t slash = rest.find(L'/');
    std::wstring host = (slash == std::wstring::npos) ? rest : rest.substr(0, slash);

    if (relative_url[0] == L'/') {
        return scheme + host + relative_url;
    }

    std::wstring path = (slash == std::wstring::npos) ? L"/" : rest.substr(slash);
    size_t last_slash = path.find_last_of(L'/');
    std::wstring dir = (last_slash == std::wstring::npos) ? L"/" : path.substr(0, last_slash + 1);
    return scheme + host + dir + relative_url;
}

} // namespace engine
