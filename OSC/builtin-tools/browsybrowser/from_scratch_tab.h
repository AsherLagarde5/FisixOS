#pragma once
#include "engine_dom.h"
#include "engine_html.h"
#include "engine_css.h"
#include "engine_layout.h"
#include "engine_network.h"
#include "engine_renderer.h"
#include "browser_profile.h"
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

namespace engine {

class FromScratchTab {
public:
    std::wstring url = L"browsy://newtab";
    std::wstring title = L"New Tab";
    bool is_loading = false;
    int status_code = 200;
    std::wstring raw_html;
    std::wstring error_message;

    std::shared_ptr<DOMDocument> doc;
    std::unique_ptr<LayoutBox> layout_root;

    std::vector<std::wstring> history;
    size_t history_index = 0;

    int scroll_y = 0;
    int max_scroll_y = 0;
    int document_height = 0;
    int last_viewport_width = 800;

    std::wstring hovered_href;

    // Tab Rectangles in Chrome UI
    RECT tab_rect = {};
    RECT close_btn_rect = {};

    FromScratchTab(const std::wstring& initial_url = L"browsy://newtab") {
        url = initial_url;
    }

    bool can_go_back() const {
        return history_index > 0 && !history.empty();
    }

    bool can_go_forward() const {
        return history_index + 1 < history.size();
    }

    void go_back(HDC hdc, int viewport_width) {
        if (can_go_back()) {
            --history_index;
            navigate(history[history_index], false, hdc, viewport_width);
        }
    }

    void go_forward(HDC hdc, int viewport_width) {
        if (can_go_forward()) {
            ++history_index;
            navigate(history[history_index], false, hdc, viewport_width);
        }
    }

    void reload(HDC hdc, int viewport_width) {
        navigate(url, false, hdc, viewport_width);
    }

    void navigate(const std::wstring& target_url, bool record_history, HDC hdc, int viewport_width) {
        url = target_url;
        if (url.empty()) url = L"browsy://newtab";

        if (record_history) {
            if (history.empty() || history[history_index] != url) {
                history.erase(history.begin() + history_index + (history.empty() ? 0 : 1), history.end());
                history.push_back(url);
                history_index = history.size() - 1;
            }
        }

        is_loading = true;
        scroll_y = 0;

        // Fetch document
        auto resp = NetworkClient::fetch(url);
        is_loading = false;
        status_code = resp.status_code;

        if (resp.success) {
            raw_html = resp.body_text;
            error_message = L"";

            // Parse HTML to DOM
            doc = HTMLParser::parse(raw_html, url);
            title = !doc->title.empty() ? doc->title : (url == L"browsy://newtab" ? L"New Tab" : url);

            // Load linked stylesheets before layout so external page styling is available.
            for (const auto& stylesheet_url : doc->stylesheet_urls) {
                auto stylesheet = NetworkClient::fetch(resolve_url(url, stylesheet_url));
                if (stylesheet.success && !stylesheet.body_text.empty()) {
                    doc->embedded_stylesheets.push_back(stylesheet.body_text);
                }
            }

            // Reflow layout
            reflow(hdc, viewport_width);

            // Record to Profile History
            if (url.rfind(L"browsy://", 0) != 0 && url != L"about:blank") {
                ProfileManager::instance().add_history(title, url);
            }
        } else {
            raw_html = L"<h1>Page unavailable</h1><p>" + resp.error_message + L"</p>";
            error_message = resp.error_message;
            doc = HTMLParser::parse(raw_html, url);
            title = L"Error: " + url;
            reflow(hdc, viewport_width);
        }
    }

    void reflow(HDC hdc, int viewport_width) {
        last_viewport_width = viewport_width;
        if (!doc) return;

        layout_root = LayoutEngine::layout_document(doc, viewport_width, hdc);
        if (layout_root) {
            document_height = layout_root->bounds.y + layout_root->bounds.height + 40;
        } else {
            document_height = 0;
        }
    }

    void paint(HDC hdc, const RECT& viewport) {
        // Draw document background (matches body / Android theme)
        HBRUSH bg_brush = CreateSolidBrush(RGB(10, 14, 22));
        FillRect(hdc, &viewport, bg_brush);
        DeleteObject(bg_brush);

        // Subtle separator under chrome
        RECT sep = { viewport.left, viewport.top, viewport.right, viewport.top + 1 };
        HBRUSH sep_br = CreateSolidBrush(RGB(42, 54, 82));
        FillRect(hdc, &sep, sep_br);
        DeleteObject(sep_br);

        if (is_loading) {
            RECT bar = { viewport.left, viewport.top + 1, viewport.right, viewport.top + 4 };
            HBRUSH load_br = CreateSolidBrush(RGB(0, 240, 192));
            FillRect(hdc, &bar, load_br);
            DeleteObject(load_br);
        }

        if (layout_root) {
            int viewport_h = viewport.bottom - viewport.top;
            max_scroll_y = (std::max)(0, document_height - viewport_h);
            scroll_y = std::clamp(scroll_y, 0, max_scroll_y);

            Renderer::paint_layout_tree(hdc, layout_root.get(), scroll_y, viewport, hovered_href);

            // Paint Custom Scrollbar Indicator if document overflows
            if (max_scroll_y > 0) {
                int bar_w = 6;
                int track_h = viewport_h;
                int thumb_h = (std::max)(24, (viewport_h * viewport_h) / document_height);
                int thumb_y = viewport.top + (scroll_y * (track_h - thumb_h)) / max_scroll_y;

                RECT thumb_r = { viewport.right - bar_w - 2, thumb_y, viewport.right - 2, thumb_y + thumb_h };
                HBRUSH thumb_br = CreateSolidBrush(RGB(56, 68, 87));
                FillRect(hdc, &thumb_r, thumb_br);
                DeleteObject(thumb_br);
            }
        }
    }

    bool handle_mouse_move(int screen_x, int screen_y, const RECT& viewport) {
        if (!layout_root) return false;

        int doc_x = screen_x - viewport.left;
        int doc_y = screen_y - viewport.top + scroll_y;

        std::wstring new_hover = layout_root->get_href_at(doc_x, doc_y);
        bool changed = (new_hover != hovered_href);
        hovered_href = new_hover;

        return !hovered_href.empty();
    }

    bool handle_mouse_click(int screen_x, int screen_y, const RECT& viewport, std::wstring& out_href) {
        if (!layout_root) return false;

        int doc_x = screen_x - viewport.left;
        int doc_y = screen_y - viewport.top + scroll_y;

        std::wstring clicked = layout_root->get_href_at(doc_x, doc_y);
        if (!clicked.empty()) {
            out_href = resolve_url(url, clicked);
            return true;
        }
        return false;
    }

    void handle_mouse_wheel(int delta, int viewport_height) {
        int scroll_step = 60;
        int delta_y = (delta / 120) * scroll_step;
        max_scroll_y = (std::max)(0, document_height - viewport_height);
        scroll_y = std::clamp(scroll_y - delta_y, 0, max_scroll_y);
    }

    void handle_key_scroll(WPARAM key, int viewport_height) {
        max_scroll_y = (std::max)(0, document_height - viewport_height);
        if (key == VK_UP) scroll_y = (std::max)(0, scroll_y - 40);
        else if (key == VK_DOWN) scroll_y = (std::min)(max_scroll_y, scroll_y + 40);
        else if (key == VK_PRIOR) scroll_y = (std::max)(0, scroll_y - viewport_height + 40);
        else if (key == VK_NEXT) scroll_y = (std::min)(max_scroll_y, scroll_y + viewport_height - 40);
        else if (key == VK_HOME) scroll_y = 0;
        else if (key == VK_END) scroll_y = max_scroll_y;
    }
};

} // namespace engine
