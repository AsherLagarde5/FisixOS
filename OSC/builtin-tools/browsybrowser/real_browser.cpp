#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <fstream>

#include "browser_theme.h"
#include "browser_profile.h"
#include "internal_pages.h"
#include "from_scratch_tab.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

// UI Layout Constants (Extra Large Modern Sizing)
constexpr int TAB_BAR_HEIGHT     = 52;
constexpr int TOOLBAR_HEIGHT     = 58;
constexpr int BOOKMARKS_HEIGHT   = 40;
constexpr int MIN_TAB_WIDTH      = 150;
constexpr int MAX_TAB_WIDTH      = 280;
constexpr int NAV_BTN_SIZE       = 42;
constexpr int CAPTION_BTN_WIDTH  = 48;
constexpr int RESIZE_BORDER      = 6;

// Control IDs
constexpr int IDC_ADDRESS_BAR    = 1001;
constexpr int IDC_FIND_INPUT     = 1002;

enum class ElementType {
    None,
    Tab,
    TabClose,
    NewTabBtn,
    NavBack,
    NavForward,
    NavReload,
    NavHome,
    AddressPill,
    BookmarkStar,
    CopyUrlBtn,
    ToolFind,
    ToolBookmarks,
    ToolHistory,
    ToolSettings,
    ToolViewSource,
    BookmarkChip,
    WinMin,
    WinMax,
    WinClose,
    DocumentArea
};

struct HitTarget {
    ElementType type = ElementType::None;
    int index = -1;
    std::wstring extra;
};

class BrowserApp {
public:
    HWND hwnd = nullptr;
    HWND address_bar = nullptr;
    HWND find_input = nullptr;
    WNDPROC old_address_proc = nullptr;
    WNDPROC old_find_proc = nullptr;

    theme::Fonts fonts;
    std::vector<std::unique_ptr<engine::FromScratchTab>> tabs;
    size_t active_tab_index = 0;

    bool is_fullscreen = false;
    bool find_bar_open = false;
    std::wstring find_text;

    // Hover & UI State Tracking
    HitTarget hovered_target;
    bool is_tracking_mouse = false;

    // Layout Rectangles
    RECT rect_client = {};
    RECT rect_tabs = {};
    RECT rect_toolbar = {};
    RECT rect_bookmarks = {};
    RECT rect_new_tab = {};
    RECT rect_back = {};
    RECT rect_forward = {};
    RECT rect_reload = {};
    RECT rect_home = {};
    RECT rect_omnibox = {};
    RECT rect_star = {};
    RECT rect_copy = {};
    RECT rect_tool_find = {};
    RECT rect_tool_bm = {};
    RECT rect_tool_hist = {};
    RECT rect_tool_src = {};
    RECT rect_tool_set = {};
    RECT rect_win_min = {};
    RECT rect_win_max = {};
    RECT rect_win_close = {};
    std::vector<std::pair<RECT, size_t>> bookmark_chip_rects;

    static BrowserApp& instance() {
        static BrowserApp s_app;
        return s_app;
    }

    int get_header_height() const {
        if (is_fullscreen) return 0;
        int h = TAB_BAR_HEIGHT + TOOLBAR_HEIGHT;
        if (ProfileManager::instance().settings.show_bookmarks_bar) {
            h += BOOKMARKS_HEIGHT;
        }
        return h;
    }

    RECT get_viewport_bounds() const {
        if (is_fullscreen) return rect_client;
        int top = get_header_height();
        return RECT{ 0, top, rect_client.right, rect_client.bottom };
    }

    engine::FromScratchTab* active_tab() {
        if (tabs.empty() || active_tab_index >= tabs.size()) return nullptr;
        return tabs[active_tab_index].get();
    }

    void init(HWND window_hwnd) {
        hwnd = window_hwnd;
        fonts.init();
        ProfileManager::instance().init();
        engine::Renderer::init_gdiplus();

        // Enable Windows 11/10 DWM dark title bar
        BOOL dark_mode = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark_mode, sizeof(dark_mode));
        DwmSetWindowAttribute(hwnd, 19 /* DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 */, &dark_mode, sizeof(dark_mode));

        // Extend frame into client area for seamless titlebar
        MARGINS margins = { 0, 0, 1, 0 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);

        GetClientRect(hwnd, &rect_client);
        if (rect_client.right <= 0 || rect_client.bottom <= 0) {
            rect_client = { 0, 0, 1280, 820 };
        }

        // Create Subclassed Address Bar (Edit Control)
        address_bar = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 100, 26,
            hwnd, (HMENU)(INT_PTR)IDC_ADDRESS_BAR, GetModuleHandleW(nullptr), nullptr
        );
        SendMessageW(address_bar, WM_SETFONT, (WPARAM)fonts.ui, TRUE);
        old_address_proc = (WNDPROC)SetWindowLongPtrW(address_bar, GWLP_WNDPROC, (LONG_PTR)address_bar_subclass);

        // Create Subclassed Find Bar Input
        find_input = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | ES_AUTOHSCROLL,
            0, 0, 160, 26,
            hwnd, (HMENU)(INT_PTR)IDC_FIND_INPUT, GetModuleHandleW(nullptr), nullptr
        );
        SendMessageW(find_input, WM_SETFONT, (WPARAM)fonts.ui, TRUE);
        old_find_proc = (WNDPROC)SetWindowLongPtrW(find_input, GWLP_WNDPROC, (LONG_PTR)find_input_subclass);

        update_layout();

        // Open Initial Tab
        create_new_tab(ProfileManager::instance().settings.homepage);
    }

    void create_new_tab(const std::wstring& target_url = L"browsy://newtab") {
        auto tab = std::make_unique<engine::FromScratchTab>(target_url);

        HDC hdc = GetDC(hwnd);
        RECT vp = get_viewport_bounds();
        int vp_w = (std::max)((int)(vp.right - vp.left), 400);
        tab->navigate(target_url, true, hdc, vp_w);
        ReleaseDC(hwnd, hdc);

        tabs.push_back(std::move(tab));
        active_tab_index = tabs.size() - 1;

        update_window_title();
        update_layout();
        update_address_bar_text();
        request_repaint();
    }

    void switch_to_tab(size_t index) {
        if (index >= tabs.size() || index == active_tab_index) return;
        active_tab_index = index;

        update_window_title();
        update_address_bar_text();
        request_repaint();
    }

    void close_tab(size_t index) {
        if (index >= tabs.size()) return;

        ProfileManager::instance().push_closed_tab(tabs[index]->url, tabs[index]->title);
        tabs.erase(tabs.begin() + index);

        if (tabs.empty()) {
            create_new_tab(ProfileManager::instance().settings.homepage);
            return;
        }

        if (active_tab_index >= tabs.size()) {
            active_tab_index = tabs.size() - 1;
        }

        update_window_title();
        update_layout();
        update_address_bar_text();
        request_repaint();
    }

    void reopen_closed_tab() {
        std::wstring url, title;
        if (ProfileManager::instance().pop_closed_tab(url, title)) {
            create_new_tab(url);
        }
    }

    void update_window_title() {
        if (active_tab()) {
            SetWindowTextW(hwnd, (active_tab()->title + L" - browsybrowser").c_str());
        }
    }

    void navigate_current(const std::wstring& input) {
        if (!active_tab()) return;

        std::wstring text = engine::trim(input);
        if (text.empty()) return;

        std::wstring final_url;
        if (text.rfind(L"browsy://", 0) == 0 || text.rfind(L"about:", 0) == 0 || text.rfind(L"file://", 0) == 0 || text.rfind(L"bezcp://", 0) == 0) {
            final_url = text;
        } else if (text.find(L"://") != std::wstring::npos) {
            final_url = text;
        } else if (text.find(L'.') != std::wstring::npos && text.find(L' ') == std::wstring::npos) {
            final_url = L"https://" + text;
        } else if (text.rfind(L"localhost", 0) == 0 || text.rfind(L"127.0.0.1", 0) == 0) {
            final_url = L"http://" + text;
        } else {
            final_url = ProfileManager::instance().get_search_url(text);
        }

        HDC hdc = GetDC(hwnd);
        RECT vp = get_viewport_bounds();
        int vp_w = (std::max)((int)(vp.right - vp.left), 400);
        active_tab()->navigate(final_url, true, hdc, vp_w);
        ReleaseDC(hwnd, hdc);

        update_window_title();
        update_address_bar_text();
        request_repaint();
    }

    void update_address_bar_text() {
        if (!active_tab() || !address_bar) return;
        std::wstring display_url = active_tab()->url;
        if (display_url == L"browsy://newtab" || display_url == L"about:blank") {
            display_url = L"";
        }
        SetWindowTextW(address_bar, display_url.c_str());
    }

    void update_layout() {
        GetClientRect(hwnd, &rect_client);
        int w = rect_client.right;
        int h = rect_client.bottom;

        if (is_fullscreen) return;

        // 1. Caption Buttons on top right (Minimize, Maximize, Close)
        int cap_w = 48;
        int cap_h = TAB_BAR_HEIGHT;
        rect_win_close = RECT{ w - cap_w, 0, w, cap_h };
        rect_win_max   = RECT{ w - cap_w * 2, 0, w - cap_w, cap_h };
        rect_win_min   = RECT{ w - cap_w * 3, 0, w - cap_w * 2, cap_h };

        // 2. Tab Bar Rectangles
        rect_tabs = RECT{ 0, 0, w, TAB_BAR_HEIGHT };
        int caption_reserved_w = cap_w * 3 + 12;
        int avail_tab_space = (std::max)((int)(w - caption_reserved_w - 70), 150);
        int num_tabs = (int)tabs.size();
        int tab_w = num_tabs > 0 ? std::clamp(avail_tab_space / num_tabs, MIN_TAB_WIDTH, MAX_TAB_WIDTH) : MAX_TAB_WIDTH;

        int current_x = 12;
        for (size_t i = 0; i < tabs.size(); ++i) {
            tabs[i]->tab_rect = RECT{ current_x, 8, current_x + tab_w, TAB_BAR_HEIGHT };
            tabs[i]->close_btn_rect = RECT{ current_x + tab_w - 30, 16, current_x + tab_w - 8, 38 };
            current_x += tab_w + 4;
        }
        rect_new_tab = RECT{ current_x + 4, 10, current_x + 42, TAB_BAR_HEIGHT - 4 };

        // 3. Toolbar Rectangles (Height 58)
        int bar_y = TAB_BAR_HEIGHT;
        rect_toolbar = RECT{ 0, bar_y, w, bar_y + TOOLBAR_HEIGHT };

        int btn_y = bar_y + (TOOLBAR_HEIGHT - NAV_BTN_SIZE) / 2;
        rect_back     = RECT{ 12, btn_y, 12 + NAV_BTN_SIZE, btn_y + NAV_BTN_SIZE };
        rect_forward  = RECT{ 58, btn_y, 58 + NAV_BTN_SIZE, btn_y + NAV_BTN_SIZE };
        rect_reload   = RECT{ 104, btn_y, 104 + NAV_BTN_SIZE, btn_y + NAV_BTN_SIZE };
        rect_home     = RECT{ 150, btn_y, 150 + NAV_BTN_SIZE, btn_y + NAV_BTN_SIZE };

        int right_tools_w = 3 * (NAV_BTN_SIZE + 6) + 12;
        int omni_left = 202;
        int omni_right = (std::max)(omni_left + 200, (int)(w - right_tools_w - 16));
        int omni_h = 42;
        int omni_y = bar_y + (TOOLBAR_HEIGHT - omni_h) / 2;
        rect_omnibox = RECT{ omni_left, omni_y, omni_right, omni_y + omni_h };

        rect_star = RECT{ omni_right - 74, omni_y + 4, omni_right - 40, omni_y + omni_h - 4 };
        rect_copy = RECT{ omni_right - 38, omni_y + 4, omni_right - 6, omni_y + omni_h - 4 };

        int edit_x = omni_left + 40;
        int edit_w = (std::max)(20, (int)((omni_right - 80) - edit_x));
        MoveWindow(address_bar, edit_x, omni_y + 7, edit_w, 28, TRUE);

        int tool_x = omni_right + 12;
        rect_tool_bm    = RECT{ tool_x, btn_y, tool_x + NAV_BTN_SIZE, btn_y + NAV_BTN_SIZE }; tool_x += NAV_BTN_SIZE + 6;
        rect_tool_hist  = RECT{ tool_x, btn_y, tool_x + NAV_BTN_SIZE, btn_y + NAV_BTN_SIZE }; tool_x += NAV_BTN_SIZE + 6;
        rect_tool_src   = RECT{ tool_x, btn_y, tool_x + NAV_BTN_SIZE, btn_y + NAV_BTN_SIZE };

        // 4. Bookmarks Bar Rectangles (Height 40)
        int current_top = bar_y + TOOLBAR_HEIGHT;
        if (ProfileManager::instance().settings.show_bookmarks_bar) {
            rect_bookmarks = RECT{ 0, current_top, w, current_top + BOOKMARKS_HEIGHT };
            bookmark_chip_rects.clear();
            int bm_x = 14;
            const auto& bm_list = ProfileManager::instance().bookmarks;
            for (size_t i = 0; i < bm_list.size() && bm_x < w - 80; ++i) {
                int chip_w = 80 + (int)bm_list[i].title.length() * 8;
                chip_w = std::clamp(chip_w, 80, 200);
                RECT r = { bm_x, current_top + 4, bm_x + chip_w, current_top + BOOKMARKS_HEIGHT - 4 };
                bookmark_chip_rects.push_back({ r, i });
                bm_x += chip_w + 8;
            }
            current_top += BOOKMARKS_HEIGHT;
        }

        // Reflow Active Tab on Resize
        if (active_tab()) {
            HDC hdc = GetDC(hwnd);
            RECT vp = get_viewport_bounds();
            int vp_w = (std::max)((int)(vp.right - vp.left), 400);
            active_tab()->reflow(hdc, vp_w);
            ReleaseDC(hwnd, hdc);
        }
    }

    HitTarget hit_test(int x, int y) {
        POINT pt = { x, y };

        if (PtInRect(&rect_win_close, pt)) return { ElementType::WinClose, -1 };
        if (PtInRect(&rect_win_max, pt)) return { ElementType::WinMax, -1 };
        if (PtInRect(&rect_win_min, pt)) return { ElementType::WinMin, -1 };

        if (PtInRect(&rect_new_tab, pt)) return { ElementType::NewTabBtn, -1 };
        for (size_t i = 0; i < tabs.size(); ++i) {
            if (PtInRect(&tabs[i]->close_btn_rect, pt)) return { ElementType::TabClose, (int)i };
            if (PtInRect(&tabs[i]->tab_rect, pt)) return { ElementType::Tab, (int)i };
        }

        if (PtInRect(&rect_back, pt)) return { ElementType::NavBack, -1 };
        if (PtInRect(&rect_forward, pt)) return { ElementType::NavForward, -1 };
        if (PtInRect(&rect_reload, pt)) return { ElementType::NavReload, -1 };
        if (PtInRect(&rect_home, pt)) return { ElementType::NavHome, -1 };
        if (PtInRect(&rect_star, pt)) return { ElementType::BookmarkStar, -1 };
        if (PtInRect(&rect_copy, pt)) return { ElementType::CopyUrlBtn, -1 };
        if (PtInRect(&rect_tool_bm, pt)) return { ElementType::ToolBookmarks, -1 };
        if (PtInRect(&rect_tool_hist, pt)) return { ElementType::ToolHistory, -1 };
        if (PtInRect(&rect_tool_src, pt)) return { ElementType::ToolViewSource, -1 };

        if (ProfileManager::instance().settings.show_bookmarks_bar) {
            for (const auto& item : bookmark_chip_rects) {
                if (PtInRect(&item.first, pt)) return { ElementType::BookmarkChip, (int)item.second };
            }
        }

        RECT vp = get_viewport_bounds();
        if (PtInRect(&vp, pt)) return { ElementType::DocumentArea, -1 };

        return { ElementType::None, -1 };
    }

    void handle_mouse_down(int x, int y, UINT button) {
        HitTarget target = hit_test(x, y);

        if (button == WM_LBUTTONDOWN) {
            switch (target.type) {
            case ElementType::WinMin:
                ShowWindow(hwnd, SW_MINIMIZE);
                break;
            case ElementType::WinMax:
                ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                break;
            case ElementType::WinClose:
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                break;
            case ElementType::Tab:
                switch_to_tab(target.index);
                break;
            case ElementType::TabClose:
                close_tab(target.index);
                break;
            case ElementType::NewTabBtn:
                create_new_tab(ProfileManager::instance().settings.homepage);
                break;
            case ElementType::NavBack:
                if (active_tab()) {
                    HDC hdc = GetDC(hwnd);
                    RECT vp = get_viewport_bounds();
                    active_tab()->go_back(hdc, vp.right - vp.left);
                    ReleaseDC(hwnd, hdc);
                    update_window_title();
                    update_address_bar_text();
                }
                break;
            case ElementType::NavForward:
                if (active_tab()) {
                    HDC hdc = GetDC(hwnd);
                    RECT vp = get_viewport_bounds();
                    active_tab()->go_forward(hdc, vp.right - vp.left);
                    ReleaseDC(hwnd, hdc);
                    update_window_title();
                    update_address_bar_text();
                }
                break;
            case ElementType::NavReload:
                if (active_tab()) {
                    HDC hdc = GetDC(hwnd);
                    RECT vp = get_viewport_bounds();
                    active_tab()->reload(hdc, vp.right - vp.left);
                    ReleaseDC(hwnd, hdc);
                    update_window_title();
                    update_address_bar_text();
                }
                break;
            case ElementType::NavHome:
                navigate_current(ProfileManager::instance().settings.homepage);
                break;
            case ElementType::BookmarkStar:
                if (active_tab()) {
                    ProfileManager::instance().toggle_bookmark(active_tab()->title, active_tab()->url);
                    update_layout();
                }
                break;
            case ElementType::CopyUrlBtn:
                if (active_tab()) copy_to_clipboard(active_tab()->url);
                break;
            case ElementType::ToolBookmarks:
                navigate_current(L"browsy://bookmarks");
                break;
            case ElementType::ToolHistory:
                navigate_current(L"browsy://history");
                break;
            case ElementType::ToolViewSource:
                show_view_source();
                break;
            case ElementType::BookmarkChip:
                if (target.index >= 0 && (size_t)target.index < ProfileManager::instance().bookmarks.size()) {
                    navigate_current(ProfileManager::instance().bookmarks[target.index].url);
                }
                break;
            case ElementType::DocumentArea:
                if (active_tab()) {
                    RECT vp = get_viewport_bounds();
                    std::wstring clicked_href;
                    if (active_tab()->handle_mouse_click(x, y, vp, clicked_href)) {
                        navigate_current(clicked_href);
                    }
                }
                break;
            default:
                break;
            }
        } else if (button == WM_MBUTTONDOWN) {
            if (target.type == ElementType::Tab) {
                close_tab(target.index);
            }
        }
        request_repaint();
    }

    void show_view_source() {
        if (!active_tab()) return;
        MessageBoxW(hwnd, active_tab()->raw_html.c_str(), (L"Page Source: " + active_tab()->url).c_str(), MB_OK | MB_ICONINFORMATION);
    }

    void copy_to_clipboard(const std::wstring& text) {
        if (!OpenClipboard(hwnd)) return;
        EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (hg) {
            memcpy(GlobalLock(hg), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(hg);
            SetClipboardData(CF_UNICODETEXT, hg);
        }
        CloseClipboard();
    }

    void request_repaint() {
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void paint(HDC hdc) {
        GetClientRect(hwnd, &rect_client);
        if (rect_client.right <= 0 || rect_client.bottom <= 0) return;

        HDC mem_dc = CreateCompatibleDC(hdc);
        HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, rect_client.right, rect_client.bottom);
        HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

        if (!is_fullscreen) {
            // 1. Paint Tab Bar (Seamless Top Header)
            theme::fill_rect(mem_dc, rect_tabs, theme::BG_TITLEBAR);

            // Paint Caption Buttons on top-right
            bool is_close_hover = (hovered_target.type == ElementType::WinClose);
            bool is_max_hover   = (hovered_target.type == ElementType::WinMax);
            bool is_min_hover   = (hovered_target.type == ElementType::WinMin);

            if (is_close_hover) theme::fill_rect(mem_dc, rect_win_close, theme::BG_CLOSE_HOVER);
            if (is_max_hover)   theme::fill_rect(mem_dc, rect_win_max, theme::BG_TAB_HOVER);
            if (is_min_hover)   theme::fill_rect(mem_dc, rect_win_min, theme::BG_TAB_HOVER);

            theme::draw_text_centered(mem_dc, L"✕", rect_win_close, is_close_hover ? RGB(255, 255, 255) : theme::TEXT_MUTED, fonts.ui_bold);
            theme::draw_text_centered(mem_dc, IsZoomed(hwnd) ? L"❐" : L"□", rect_win_max, is_max_hover ? RGB(255, 255, 255) : theme::TEXT_MUTED, fonts.ui_bold);
            theme::draw_text_centered(mem_dc, L"─", rect_win_min, is_min_hover ? RGB(255, 255, 255) : theme::TEXT_MUTED, fonts.ui_bold);

            // Paint Tabs
            for (size_t i = 0; i < tabs.size(); ++i) {
                bool is_active = (i == active_tab_index);
                bool is_tab_hover = (hovered_target.type == ElementType::Tab && hovered_target.index == (int)i);
                bool is_tab_close_hover = (hovered_target.type == ElementType::TabClose && hovered_target.index == (int)i);

                COLORREF tab_bg = is_active ? theme::BG_TOOLBAR : (is_tab_hover ? theme::BG_TAB_HOVER : theme::BG_TAB_INACTIVE);
                COLORREF text_color = is_active ? theme::TEXT_PRIMARY : (is_tab_hover ? theme::TEXT_PRIMARY : theme::TEXT_MUTED);

                theme::draw_rounded_rect(mem_dc, tabs[i]->tab_rect, 12, tab_bg, is_active ? theme::BORDER_DARK : theme::BG_TITLEBAR, is_active ? 1 : 0);

                if (is_active) {
                    // Seamless bottom bridge to toolbar
                    RECT bridge = { tabs[i]->tab_rect.left, tabs[i]->tab_rect.bottom - 4, tabs[i]->tab_rect.right, tabs[i]->tab_rect.bottom + 2 };
                    theme::fill_rect(mem_dc, bridge, theme::BG_TOOLBAR);

                    // Glowing top accent indicator
                    RECT accent_rect = { tabs[i]->tab_rect.left + 12, tabs[i]->tab_rect.top, tabs[i]->tab_rect.right - 12, tabs[i]->tab_rect.top + 3 };
                    theme::fill_rect(mem_dc, accent_rect, theme::ACCENT_TEAL);
                } else if (i + 1 < tabs.size() && (i + 1) != active_tab_index) {
                    // Subtle tab divider between adjacent inactive tabs
                    RECT divider = { tabs[i]->tab_rect.right + 1, tabs[i]->tab_rect.top + 10, tabs[i]->tab_rect.right + 2, tabs[i]->tab_rect.bottom - 10 };
                    theme::fill_rect(mem_dc, divider, theme::BORDER_DARK);
                }

                RECT title_rect = { tabs[i]->tab_rect.left + 14, tabs[i]->tab_rect.top, tabs[i]->tab_rect.right - 36, tabs[i]->tab_rect.bottom };
                std::wstring tab_title = tabs[i]->is_loading ? (L"⏳ " + tabs[i]->title) : tabs[i]->title;
                theme::draw_text_left(mem_dc, tab_title, title_rect, text_color, fonts.tab_font, true);

                if (is_tab_close_hover) {
                    theme::draw_rounded_rect(mem_dc, tabs[i]->close_btn_rect, 8, theme::BG_CLOSE_HOVER, theme::BG_CLOSE_HOVER, 0);
                    theme::draw_text_centered(mem_dc, L"×", tabs[i]->close_btn_rect, RGB(255, 255, 255), fonts.ui_bold);
                } else {
                    theme::draw_text_centered(mem_dc, L"×", tabs[i]->close_btn_rect, is_active ? theme::TEXT_MUTED : theme::TEXT_DISABLED, fonts.ui_bold);
                }
            }

            // New Tab '+' Button
            bool is_new_hover = (hovered_target.type == ElementType::NewTabBtn);
            theme::draw_rounded_rect(mem_dc, rect_new_tab, 10, is_new_hover ? theme::BG_BTN_HOVER : theme::BG_TITLEBAR, theme::BORDER_DARK, is_new_hover ? 1 : 0);
            theme::draw_text_centered(mem_dc, L"+", rect_new_tab, is_new_hover ? theme::ACCENT_TEAL : theme::TEXT_MUTED, fonts.ui_bold);

            // 2. Paint Toolbar (Height 58)
            theme::fill_rect(mem_dc, rect_toolbar, theme::BG_TOOLBAR);

            auto draw_btn = [&](const RECT& r, const wchar_t* glyph, bool enabled, ElementType type) {
                bool is_hover = (hovered_target.type == type);
                if (is_hover && enabled) {
                    theme::draw_rounded_rect(mem_dc, r, 10, theme::BG_BTN_HOVER, theme::BORDER_DARK, 1);
                }
                COLORREF col = !enabled ? theme::TEXT_DISABLED : (is_hover ? theme::ACCENT_TEAL : theme::TEXT_PRIMARY);
                theme::draw_text_centered(mem_dc, glyph, r, col, fonts.icon_font);
            };

            bool can_back = active_tab() && active_tab()->can_go_back();
            bool can_fwd = active_tab() && active_tab()->can_go_forward();
            bool is_loading = active_tab() && active_tab()->is_loading;

            draw_btn(rect_back, L"←", can_back, ElementType::NavBack);
            draw_btn(rect_forward, L"→", can_fwd, ElementType::NavForward);
            draw_btn(rect_reload, is_loading ? L"✕" : L"⟳", true, ElementType::NavReload);
            draw_btn(rect_home, L"⌂", true, ElementType::NavHome);

            // Omnibox Pill
            theme::draw_rounded_rect(mem_dc, rect_omnibox, 22, theme::BG_OMNIBOX, theme::BORDER_DARK, 1);

            RECT sec_rect = { rect_omnibox.left + 10, rect_omnibox.top, rect_omnibox.left + 36, rect_omnibox.bottom };
            if (active_tab()) {
                if (active_tab()->url.rfind(L"https://", 0) == 0) {
                    theme::draw_text_centered(mem_dc, L"🔒", sec_rect, theme::ACCENT_GREEN, fonts.icon_font);
                } else if (active_tab()->url.rfind(L"browsy://", 0) == 0 || active_tab()->url == L"about:blank") {
                    theme::draw_text_centered(mem_dc, L"🌐", sec_rect, theme::ACCENT_TEAL, fonts.icon_font);
                } else {
                    theme::draw_text_centered(mem_dc, L"⚠️", sec_rect, theme::ACCENT_RED, fonts.icon_font);
                }
            }

            bool bookmarked = active_tab() && ProfileManager::instance().is_bookmarked(active_tab()->url);
            bool star_hover = (hovered_target.type == ElementType::BookmarkStar);
            if (star_hover) theme::draw_rounded_rect(mem_dc, rect_star, 8, theme::BG_BTN_HOVER, theme::BORDER_DARK, 1);
            theme::draw_text_centered(mem_dc, bookmarked ? L"★" : L"☆", rect_star, bookmarked ? theme::ACCENT_GOLD : (star_hover ? theme::ACCENT_GOLD : theme::TEXT_MUTED), fonts.icon_font);

            bool copy_hover = (hovered_target.type == ElementType::CopyUrlBtn);
            if (copy_hover) theme::draw_rounded_rect(mem_dc, rect_copy, 8, theme::BG_BTN_HOVER, theme::BORDER_DARK, 1);
            theme::draw_text_centered(mem_dc, L"📋", rect_copy, copy_hover ? theme::ACCENT_TEAL : theme::TEXT_MUTED, fonts.icon_font);

            draw_btn(rect_tool_bm, L"⭐", true, ElementType::ToolBookmarks);
            draw_btn(rect_tool_hist, L"🕒", true, ElementType::ToolHistory);
            draw_btn(rect_tool_src, L"📄", true, ElementType::ToolViewSource);

            // 3. Bookmarks Bar
            if (ProfileManager::instance().settings.show_bookmarks_bar) {
                theme::fill_rect(mem_dc, rect_bookmarks, theme::BG_BOOKMARKS);
                const auto& bm_list = ProfileManager::instance().bookmarks;
                for (const auto& item : bookmark_chip_rects) {
                    bool chip_hover = (hovered_target.type == ElementType::BookmarkChip && hovered_target.index == (int)item.second);
                    if (chip_hover) {
                        theme::draw_rounded_rect(mem_dc, item.first, 14, theme::BG_BTN_HOVER, theme::BORDER_ACCENT, 1);
                    } else {
                        theme::draw_rounded_rect(mem_dc, item.first, 14, theme::BG_TAB_ACTIVE, theme::BORDER_DARK, 1);
                    }
                    std::wstring label = L"★ " + bm_list[item.second].title;
                    RECT text_r = { item.first.left + 12, item.first.top, item.first.right - 12, item.first.bottom };
                    theme::draw_text_left(mem_dc, label, text_r, chip_hover ? theme::ACCENT_TEAL : theme::TEXT_PRIMARY, fonts.small_font, true);
                }
            }
        }

        // 4. Paint From-Scratch Document Viewport
        RECT vp = get_viewport_bounds();
        if (active_tab()) {
            active_tab()->paint(mem_dc, vp);
        }

        BitBlt(hdc, 0, 0, rect_client.right, rect_client.bottom, mem_dc, 0, 0, SRCCOPY);

        SelectObject(mem_dc, old_bmp);
        DeleteObject(mem_bmp);
        DeleteDC(mem_dc);
    }

    static LRESULT CALLBACK address_bar_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        BrowserApp& app = BrowserApp::instance();
        if (msg == WM_KEYDOWN) {
            if (wp == VK_RETURN) {
                int len = GetWindowTextLengthW(hwnd);
                std::wstring text(len, L'\0');
                GetWindowTextW(hwnd, text.data(), len + 1);
                app.navigate_current(text);
                SetFocus(app.hwnd);
                return 0;
            } else if (wp == VK_ESCAPE) {
                app.update_address_bar_text();
                SetFocus(app.hwnd);
                return 0;
            }
        }
        return CallWindowProcW(app.old_address_proc, hwnd, msg, wp, lp);
    }

    static LRESULT CALLBACK find_input_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        BrowserApp& app = BrowserApp::instance();
        return CallWindowProcW(app.old_find_proc, hwnd, msg, wp, lp);
    }

    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        BrowserApp& app = BrowserApp::instance();

        switch (msg) {
        case WM_CREATE:
            app.init(hwnd);
            return 0;

        case WM_NCCALCSIZE:
            // Extend client area to window edges
            if (wp == TRUE) {
                return 0;
            }
            break;

        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &pt);

            RECT rc;
            GetClientRect(hwnd, &rc);

            // Resize border checks
            if (!app.is_fullscreen && !IsZoomed(hwnd)) {
                if (pt.y < RESIZE_BORDER && pt.x < RESIZE_BORDER) return HTTOPLEFT;
                if (pt.y < RESIZE_BORDER && pt.x >= rc.right - RESIZE_BORDER) return HTTOPRIGHT;
                if (pt.y >= rc.bottom - RESIZE_BORDER && pt.x < RESIZE_BORDER) return HTBOTTOMLEFT;
                if (pt.y >= rc.bottom - RESIZE_BORDER && pt.x >= rc.right - RESIZE_BORDER) return HTBOTTOMRIGHT;
                if (pt.y < RESIZE_BORDER) return HTTOP;
                if (pt.y >= rc.bottom - RESIZE_BORDER) return HTBOTTOM;
                if (pt.x < RESIZE_BORDER) return HTLEFT;
                if (pt.x >= rc.right - RESIZE_BORDER) return HTRIGHT;
            }

            // Caption buttons & interactive UI are client clicks
            HitTarget target = app.hit_test(pt.x, pt.y);
            if (target.type != ElementType::None && target.type != ElementType::DocumentArea) {
                return HTCLIENT;
            }

            // Empty space in Tab Bar acts as Title Bar Drag Area
            if (pt.y < TAB_BAR_HEIGHT) {
                return HTCAPTION;
            }

            return HTCLIENT;
        }

        case WM_SIZE:
            app.update_layout();
            app.request_repaint();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            app.paint(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            HitTarget target = app.hit_test(x, y);

            bool is_link_under_cursor = false;
            if (target.type == ElementType::DocumentArea && app.active_tab()) {
                RECT vp = app.get_viewport_bounds();
                is_link_under_cursor = app.active_tab()->handle_mouse_move(x, y, vp);
            }

            if (is_link_under_cursor || target.type == ElementType::TabClose || target.type == ElementType::NewTabBtn || target.type == ElementType::BookmarkChip || target.type == ElementType::WinClose || target.type == ElementType::WinMax || target.type == ElementType::WinMin) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
            } else {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }

            if (target.type != app.hovered_target.type || target.index != app.hovered_target.index || is_link_under_cursor) {
                app.hovered_target = target;
                app.request_repaint();
            }

            if (!app.is_tracking_mouse) {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                app.is_tracking_mouse = true;
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            app.is_tracking_mouse = false;
            app.hovered_target = { ElementType::None, -1 };
            if (app.active_tab()) app.active_tab()->hovered_href.clear();
            app.request_repaint();
            return 0;

        case WM_LBUTTONDOWN:
            app.handle_mouse_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), WM_LBUTTONDOWN);
            return 0;

        case WM_MBUTTONDOWN:
            app.handle_mouse_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), WM_MBUTTONDOWN);
            return 0;

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            if (app.active_tab()) {
                RECT vp = app.get_viewport_bounds();
                app.active_tab()->handle_mouse_wheel(delta, vp.bottom - vp.top);
                app.request_repaint();
            }
            return 0;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wp;
            SetBkColor(hdc, theme::BG_OMNIBOX);
            SetTextColor(hdc, theme::TEXT_PRIMARY);
            static HBRUSH s_edit_brush = CreateSolidBrush(theme::BG_OMNIBOX);
            return (LRESULT)s_edit_brush;
        }

        case WM_KEYDOWN: {
            bool ctrl = (GetKeyState(VK_CONTROL) < 0);
            bool shift = (GetKeyState(VK_SHIFT) < 0);
            bool alt = (GetKeyState(VK_MENU) < 0);

            if (ctrl && !shift && wp == 'T') {
                app.create_new_tab(ProfileManager::instance().settings.homepage);
                return 0;
            }
            if (ctrl && shift && wp == 'T') {
                app.reopen_closed_tab();
                return 0;
            }
            if (ctrl && wp == 'W') {
                app.close_tab(app.active_tab_index);
                return 0;
            }
            if (ctrl && wp == 'U') {
                app.show_view_source();
                return 0;
            }
            if (ctrl && wp == VK_TAB) {
                if (shift) {
                    size_t prev = (app.active_tab_index + app.tabs.size() - 1) % app.tabs.size();
                    app.switch_to_tab(prev);
                } else {
                    size_t next = (app.active_tab_index + 1) % app.tabs.size();
                    app.switch_to_tab(next);
                }
                return 0;
            }
            if (ctrl && wp >= '1' && wp <= '8') {
                size_t idx = (size_t)(wp - '1');
                if (idx < app.tabs.size()) app.switch_to_tab(idx);
                return 0;
            }
            if (ctrl && wp == '9') {
                if (!app.tabs.empty()) app.switch_to_tab(app.tabs.size() - 1);
                return 0;
            }
            if ((ctrl && wp == 'L') || (alt && wp == 'D') || wp == VK_F6) {
                SetFocus(app.address_bar);
                SendMessageW(app.address_bar, EM_SETSEL, 0, -1);
                return 0;
            }
            if (wp == VK_F5 || (ctrl && wp == 'R')) {
                if (app.active_tab()) {
                    HDC hdc = GetDC(hwnd);
                    RECT vp = app.get_viewport_bounds();
                    app.active_tab()->reload(hdc, vp.right - vp.left);
                    ReleaseDC(hwnd, hdc);
                    app.update_window_title();
                    app.update_address_bar_text();
                    app.request_repaint();
                }
                return 0;
            }
            if (ctrl && wp == 'D') {
                if (app.active_tab()) {
                    ProfileManager::instance().toggle_bookmark(app.active_tab()->title, app.active_tab()->url);
                    app.update_layout();
                    app.request_repaint();
                }
                return 0;
            }
            if (ctrl && shift && wp == 'B') {
                ProfileManager::instance().settings.show_bookmarks_bar = !ProfileManager::instance().settings.show_bookmarks_bar;
                ProfileManager::instance().save();
                app.update_layout();
                app.request_repaint();
                return 0;
            }
            if (ctrl && wp == 'H') {
                app.navigate_current(L"browsy://history");
                return 0;
            }
            if (ctrl && wp == 'J') {
                app.navigate_current(L"browsy://bookmarks");
                return 0;
            }
            if (wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT || wp == VK_HOME || wp == VK_END) {
                if (app.active_tab()) {
                    RECT vp = app.get_viewport_bounds();
                    app.active_tab()->handle_key_scroll(wp, vp.bottom - vp.top);
                    app.request_repaint();
                }
                return 0;
            }
            if (alt && wp == VK_LEFT) {
                if (app.active_tab()) {
                    HDC hdc = GetDC(hwnd);
                    RECT vp = app.get_viewport_bounds();
                    app.active_tab()->go_back(hdc, vp.right - vp.left);
                    ReleaseDC(hwnd, hdc);
                    app.update_window_title();
                    app.update_address_bar_text();
                    app.request_repaint();
                }
                return 0;
            }
            if (alt && wp == VK_RIGHT) {
                if (app.active_tab()) {
                    HDC hdc = GetDC(hwnd);
                    RECT vp = app.get_viewport_bounds();
                    app.active_tab()->go_forward(hdc, vp.right - vp.left);
                    ReleaseDC(hwnd, hdc);
                    app.update_window_title();
                    app.update_address_bar_text();
                    app.request_repaint();
                }
                return 0;
            }
            if (alt && wp == VK_HOME) {
                app.navigate_current(ProfileManager::instance().settings.homepage);
                return 0;
            }
            break;
        }

        case WM_DESTROY:
            app.fonts.cleanup();
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wp, lp);
    }
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc   = BrowserApp::wnd_proc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"BrowsyFromScratchBrowser";
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(theme::BG_TITLEBAR);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"browsybrowser",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 840,
        nullptr, nullptr,
        hInstance, nullptr
    );

    if (!hwnd) {
        return 0;
    }

    ShowWindow(hwnd, (nCmdShow != 0) ? nCmdShow : SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG msg = {};
    BOOL bRet;
    while ((bRet = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (bRet == -1) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
