#pragma once
#include <windows.h>
#include <wrl.h>
#include <string>
#include <functional>
#include <WebView2.h>
#include "internal_pages.h"
#include "browser_profile.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

class BrowserTab {
public:
    HWND parent_hwnd = nullptr;
    ComPtr<ICoreWebView2Environment> env;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;

    std::wstring url = L"browsy://newtab";
    std::wstring title = L"New Tab";
    bool is_loading = false;
    bool can_go_back = false;
    bool can_go_forward = false;
    double zoom_factor = 1.0;
    bool is_fullscreen = false;

    RECT tab_rect = {};
    RECT close_btn_rect = {};
    bool is_close_hovered = false;
    bool is_tab_hovered = false;

    std::function<void(BrowserTab*)> on_title_changed;
    std::function<void(BrowserTab*)> on_url_changed;
    std::function<void(BrowserTab*)> on_loading_changed;
    std::function<void(BrowserTab*)> on_history_changed;
    std::function<void(const std::wstring&)> on_open_new_tab;
    std::function<void(BrowserTab*, bool)> on_fullscreen_changed;
    std::function<void(BrowserTab*, const std::wstring&)> on_web_message;

    BrowserTab(HWND parent, ComPtr<ICoreWebView2Environment> web_env)
        : parent_hwnd(parent), env(web_env) {}

    ~BrowserTab() {
        if (controller) {
            controller->Close();
            controller = nullptr;
            webview = nullptr;
        }
    }

    void init_controller(const RECT& bounds, std::function<void(BrowserTab*)> on_ready) {
        if (!env) return;

        env->CreateCoreWebView2Controller(parent_hwnd,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [this, bounds, on_ready](HRESULT result, ICoreWebView2Controller* ctl) -> HRESULT {
                    if (FAILED(result) || !ctl) return result;

                    controller = ctl;
                    controller->get_CoreWebView2(&webview);
                    controller->put_Bounds(bounds);
                    controller->put_IsVisible(TRUE);

                    ComPtr<ICoreWebView2Settings> settings;
                    if (SUCCEEDED(webview->get_Settings(&settings)) && settings) {
                        settings->put_IsScriptEnabled(TRUE);
                        settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                        settings->put_AreDevToolsEnabled(TRUE);
                        settings->put_IsStatusBarEnabled(TRUE);
                        settings->put_IsWebMessageEnabled(TRUE);
                    }

                    setup_event_handlers();

                    if (!url.empty()) {
                        navigate(url);
                    }

                    if (on_ready) on_ready(this);
                    return S_OK;
                }).Get());
    }

    void setup_event_handlers() {
        if (!webview) return;

        // Navigation Starting
        webview->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*) -> HRESULT {
                    is_loading = true;
                    if (on_loading_changed) on_loading_changed(this);
                    return S_OK;
                }).Get(), nullptr);

        // Navigation Completed
        webview->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                    is_loading = false;
                    update_history_state();
                    if (on_loading_changed) on_loading_changed(this);

                    // Add to profile history if not internal
                    if (!url.empty() && url.rfind(L"browsy://", 0) != 0) {
                        ProfileManager::instance().add_history(title.empty() ? url : title, url);
                    }
                    return S_OK;
                }).Get(), nullptr);

        // Source Changed
        webview->add_SourceChanged(
            Callback<ICoreWebView2SourceChangedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                    LPWSTR raw_url = nullptr;
                    if (SUCCEEDED(webview->get_Source(&raw_url)) && raw_url) {
                        url = raw_url;
                        CoTaskMemFree(raw_url);
                        if (on_url_changed) on_url_changed(this);
                    }
                    return S_OK;
                }).Get(), nullptr);

        // Document Title Changed
        webview->add_DocumentTitleChanged(
            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                [this](ICoreWebView2*, IUnknown*) -> HRESULT {
                    LPWSTR raw_title = nullptr;
                    if (SUCCEEDED(webview->get_DocumentTitle(&raw_title)) && raw_title) {
                        title = raw_title;
                        CoTaskMemFree(raw_title);
                        if (title.empty()) title = L"New Tab";
                        if (on_title_changed) on_title_changed(this);
                    }
                    return S_OK;
                }).Get(), nullptr);

        // History Changed
        webview->add_HistoryChanged(
            Callback<ICoreWebView2HistoryChangedEventHandler>(
                [this](ICoreWebView2*, IUnknown*) -> HRESULT {
                    update_history_state();
                    return S_OK;
                }).Get(), nullptr);

        // New Window Requested (intercept target="_blank" and window.open)
        webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    args->put_Handled(TRUE);
                    LPWSTR target_uri = nullptr;
                    if (SUCCEEDED(args->get_Uri(&target_uri)) && target_uri) {
                        std::wstring uri_str = target_uri;
                        CoTaskMemFree(target_uri);
                        if (on_open_new_tab) on_open_new_tab(uri_str);
                    }
                    return S_OK;
                }).Get(), nullptr);

        // Fullscreen Element Changed (e.g. YouTube fullscreen video)
        webview->add_ContainsFullScreenElementChanged(
            Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
                [this](ICoreWebView2*, IUnknown*) -> HRESULT {
                    BOOL fs = FALSE;
                    webview->get_ContainsFullScreenElement(&fs);
                    is_fullscreen = fs;
                    if (on_fullscreen_changed) on_fullscreen_changed(this, is_fullscreen);
                    return S_OK;
                }).Get(), nullptr);

        // Web Message Received (from browsy:// internal page JavaScript)
        webview->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    LPWSTR message = nullptr;
                    if (SUCCEEDED(args->get_WebMessageAsJson(&message)) && message) {
                        std::wstring msg_str = message;
                        CoTaskMemFree(message);
                        handle_web_message(msg_str);
                    }
                    return S_OK;
                }).Get(), nullptr);
    }

    void handle_web_message(const std::wstring& json_str) {
        // Extract action
        size_t act_pos = json_str.find(L"\"action\":");
        if (act_pos == std::wstring::npos) return;
        size_t q1 = json_str.find(L'\"', act_pos + 9);
        size_t q2 = json_str.find(L'\"', q1 + 1);
        if (q1 == std::wstring::npos || q2 == std::wstring::npos) return;
        std::wstring action = json_str.substr(q1 + 1, q2 - q1 - 1);

        if (action == L"navigate") {
            size_t url_pos = json_str.find(L"\"url\":");
            if (url_pos != std::wstring::npos) {
                size_t uq1 = json_str.find(L'\"', url_pos + 6);
                size_t uq2 = json_str.find(L'\"', uq1 + 1);
                if (uq1 != std::wstring::npos && uq2 != std::wstring::npos) {
                    std::wstring target = json_str.substr(uq1 + 1, uq2 - uq1 - 1);
                    navigate(target);
                }
            }
        } else if (action == L"add_bookmark") {
            std::wstring b_title, b_url;
            size_t t_pos = json_str.find(L"\"title\":");
            if (t_pos != std::wstring::npos) {
                size_t t1 = json_str.find(L'\"', t_pos + 8);
                size_t t2 = json_str.find(L'\"', t1 + 1);
                if (t1 != std::wstring::npos && t2 != std::wstring::npos) b_title = json_str.substr(t1 + 1, t2 - t1 - 1);
            }
            size_t u_pos = json_str.find(L"\"url\":");
            if (u_pos != std::wstring::npos) {
                size_t u1 = json_str.find(L'\"', u_pos + 6);
                size_t u2 = json_str.find(L'\"', u1 + 1);
                if (u1 != std::wstring::npos && u2 != std::wstring::npos) b_url = json_str.substr(u1 + 1, u2 - u1 - 1);
            }
            if (!b_url.empty()) ProfileManager::instance().add_bookmark(b_title, b_url);
        } else if (action == L"remove_bookmark") {
            size_t u_pos = json_str.find(L"\"url\":");
            if (u_pos != std::wstring::npos) {
                size_t u1 = json_str.find(L'\"', u_pos + 6);
                size_t u2 = json_str.find(L'\"', u1 + 1);
                if (u1 != std::wstring::npos && u2 != std::wstring::npos) {
                    std::wstring b_url = json_str.substr(u1 + 1, u2 - u1 - 1);
                    ProfileManager::instance().remove_bookmark(b_url);
                }
            }
        } else if (action == L"clear_history") {
            ProfileManager::instance().clear_history();
        } else if (action == L"set_setting") {
            size_t k_pos = json_str.find(L"\"key\":");
            size_t v_pos = json_str.find(L"\"value\":");
            if (k_pos != std::wstring::npos && v_pos != std::wstring::npos) {
                size_t k1 = json_str.find(L'\"', k_pos + 6);
                size_t k2 = json_str.find(L'\"', k1 + 1);
                std::wstring key = (k1 != std::wstring::npos && k2 != std::wstring::npos) ? json_str.substr(k1 + 1, k2 - k1 - 1) : L"";

                if (key == L"search_engine") {
                    size_t v1 = json_str.find(L'\"', v_pos + 8);
                    size_t v2 = json_str.find(L'\"', v1 + 1);
                    if (v1 != std::wstring::npos && v2 != std::wstring::npos) {
                        ProfileManager::instance().settings.search_engine = json_str.substr(v1 + 1, v2 - v1 - 1);
                        ProfileManager::instance().save();
                    }
                } else if (key == L"show_bookmarks_bar") {
                    bool val = json_str.find(L"true", v_pos) != std::wstring::npos;
                    ProfileManager::instance().settings.show_bookmarks_bar = val;
                    ProfileManager::instance().save();
                    if (on_history_changed) on_history_changed(this);
                }
            }
        }

        if (on_web_message) on_web_message(this, json_str);
    }

    void update_history_state() {
        if (!webview) return;
        BOOL back = FALSE, fwd = FALSE;
        webview->get_CanGoBack(&back);
        webview->get_CanGoForward(&fwd);
        can_go_back = (back != FALSE);
        can_go_forward = (fwd != FALSE);
        if (on_history_changed) on_history_changed(this);
    }

    void navigate(const std::wstring& target_url) {
        url = target_url;
        if (url.empty()) url = L"browsy://newtab";

        if (url.rfind(L"browsy://", 0) == 0 || url == L"about:blank") {
            if (url == L"browsy://newtab" || url == L"browsy://start" || url == L"about:blank") title = L"New Tab";
            else if (url == L"browsy://bookmarks") title = L"Bookmarks";
            else if (url == L"browsy://history") title = L"History";
            else if (url == L"browsy://settings") title = L"Settings";
            else if (url == L"browsy://about") title = L"About browsybrowser";

            if (webview) {
                std::string html = internal_pages::get_page_content(url);
                int wlen = MultiByteToWideChar(CP_UTF8, 0, html.c_str(), -1, nullptr, 0);
                std::wstring whtml(wlen, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, html.c_str(), -1, whtml.data(), wlen);
                webview->NavigateToString(whtml.c_str());
            }
            if (on_title_changed) on_title_changed(this);
            if (on_url_changed) on_url_changed(this);
            return;
        }

        if (webview) {
            webview->Navigate(url.c_str());
        }
    }

    void go_back() { if (webview && can_go_back) webview->GoBack(); }
    void go_forward() { if (webview && can_go_forward) webview->GoForward(); }
    void reload(bool ignore_cache = false) {
        if (url.rfind(L"browsy://", 0) == 0) {
            navigate(url);
        } else if (webview) {
            webview->Reload();
        }
    }
    void stop() { if (webview) webview->Stop(); }

    void set_visible(bool visible) {
        if (controller) controller->put_IsVisible(visible ? TRUE : FALSE);
    }

    void set_bounds(const RECT& bounds) {
        if (controller) controller->put_Bounds(bounds);
    }

    void zoom_in() {
        if (controller) {
            zoom_factor = (std::min)(zoom_factor + 0.1, 3.0);
            controller->put_ZoomFactor(zoom_factor);
        }
    }

    void zoom_out() {
        if (controller) {
            zoom_factor = (std::max)(zoom_factor - 0.1, 0.3);
            controller->put_ZoomFactor(zoom_factor);
        }
    }

    void zoom_reset() {
        if (controller) {
            zoom_factor = 1.0;
            controller->put_ZoomFactor(zoom_factor);
        }
    }

    void open_dev_tools() {
        if (webview) webview->OpenDevToolsWindow();
    }

    void print_page() {
        if (webview) webview->ExecuteScript(L"window.print();", nullptr);
    }

    void find(const std::wstring& text, bool forward = true) {
        if (!webview || text.empty()) return;
        std::wstring script = L"window.find('" + text + L"', false, " + (forward ? L"false" : L"true") + L", true, false, false, false);";
        webview->ExecuteScript(script.c_str(), nullptr);
    }
};
