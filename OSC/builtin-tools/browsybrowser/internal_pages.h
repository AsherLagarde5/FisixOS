#pragma once
#include <string>
#include <sstream>
#include "browser_profile.h"

// Rust Safety Net C-FFI Export Interface
extern "C" {
    char* rust_get_internal_page(const char* route_utf8);
    void rust_free_internal_page(char* ptr);
}

namespace internal_pages {

inline std::string get_page_content(const std::wstring& url) {
    std::string route_utf8;
    for (wchar_t c : url) {
        if (c < 128) route_utf8 += (char)c;
    }

    char* rust_html = rust_get_internal_page(route_utf8.c_str());
    if (rust_html) {
        std::string result(rust_html);
        rust_free_internal_page(rust_html);
        return result;
    }

    // Safety net fallback
    return "<!DOCTYPE html><html><body><h1>browsybrowser</h1><p>Safety net active</p></body></html>";
}

} // namespace internal_pages
