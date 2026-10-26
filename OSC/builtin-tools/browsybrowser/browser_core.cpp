#include "browser_core.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace {
std::wstring to_wide(const std::string& value) {
    if (value.empty()) return {};
    int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::string to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::string run_file(const std::string& code, const char* interpreter, const char* extension) {
    const auto path = std::filesystem::temp_directory_path() / ("browsybrowser_" + std::to_string(GetCurrentProcessId()) + extension);
    { std::ofstream file(path, std::ios::binary); file << code; }
    const std::string command = std::string(interpreter) + " \"" + path.string() + "\" 2>&1";
    FILE* pipe = _popen(command.c_str(), "r");
    std::string output;
    if (pipe) {
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe)) output += buffer;
        _pclose(pipe);
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return output;
}

bool needs_browser_dom(const std::string& code) {
    const char* browser_globals[] = {"window", "document", "navigator", "location", "XMLHttpRequest", "ActiveXObject", "Image", "alert", "google", "self"};
    for (const char* global : browser_globals) if (code.find(global) != std::string::npos) return true;
    return false;
}

std::string expand_script_tags(const std::string& html) {
    std::string result;
    size_t cursor = 0;
    while (cursor < html.size()) {
        const size_t py_start = html.find("<py>", cursor);
        const size_t js_start = html.find("<script", cursor);
        size_t start = py_start;
        bool javascript = false;
        if (js_start != std::string::npos && (start == std::string::npos || js_start < start)) {
            start = js_start;
            javascript = true;
        }
        if (start == std::string::npos) { result += html.substr(cursor); break; }
        result += html.substr(cursor, start - cursor);
        const size_t open_end = html.find('>', start);
        const std::string close_tag = javascript ? "</script>" : "</py>";
        const size_t code_start = javascript ? open_end + 1 : start + 4;
        const size_t end = open_end == std::string::npos ? std::string::npos : html.find(close_tag, code_start);
        if (end == std::string::npos) { result += javascript ? "<p>Unclosed &lt;script&gt; tag.</p>" : "<p>Unclosed &lt;py&gt; tag.</p>"; break; }
        const std::string code = html.substr(code_start, end - code_start);
        if (!javascript || !needs_browser_dom(code)) {
            const char* interpreter = javascript ? "node" : "python";
            const char* extension = javascript ? ".js" : ".py";
            result += "<pre>" + run_file(code, interpreter, extension) + "</pre>";
        }
        cursor = end + close_tag.size();
    }
    return result;
}

std::string fetch(const std::wstring& url, std::wstring& error) {
    const bool bezcp = _wcsnicmp(url.c_str(), L"bezcp://", 8) == 0;
    const bool https = !bezcp && _wcsnicmp(url.c_str(), L"https://", 8) == 0;
    const std::wstring prefix = bezcp ? L"bezcp://" : (https ? L"https://" : L"http://");
    if (url.rfind(prefix, 0) != 0) { error = L"Only HTTP and HTTPS addresses are supported."; return {}; }
    const std::wstring address = url.substr(prefix.size());
    const size_t slash = address.find(L'/');
    std::wstring host_port = slash == std::wstring::npos ? address : address.substr(0, slash);
    const std::wstring path = slash == std::wstring::npos ? L"/" : address.substr(slash);

    INTERNET_PORT port = https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    std::wstring host = host_port;
    const size_t colon = host_port.find(L':');
    if (colon != std::wstring::npos) {
        host = host_port.substr(0, colon);
        port = static_cast<INTERNET_PORT>(std::stoi(host_port.substr(colon + 1)));
    }

    HINTERNET session = WinHttpOpen(L"browsybrowser/0.3", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    HINTERNET connection = session ? WinHttpConnect(session, host.c_str(), port, 0) : nullptr;
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, nullptr, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    bool okay = request && WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
    std::string body;
    if (okay) {
        char buffer[8192]; DWORD read = 0;
        do { okay = WinHttpReadData(request, buffer, sizeof(buffer), &read); body.append(buffer, read); } while (okay && read);
    }
    if (request) WinHttpCloseHandle(request); if (connection) WinHttpCloseHandle(connection); if (session) WinHttpCloseHandle(session);
    if (!okay) error = L"Could not load " + url;
    return okay ? body : std::string{};
}

std::string render(const std::string& html) {
    const std::string source = expand_script_tags(html);
    std::string result = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}{\\f1 Georgia;}}{\\colortbl;\\red70\\green150\\blue130;}\\viewkind4\\uc1 ";
    std::string text;
    std::string active_link;
    bool hidden = false;
    auto escape = [](const std::string& value) {
        std::string escaped;
        for (wchar_t character : to_wide(value)) {
            if (character == L'\\' || character == L'{' || character == L'}') escaped += '\\';
            if (character == L'\n' || character == L'\r') escaped += "\\line ";
            else if (character >= 32 && character < 127) escaped += static_cast<char>(character);
            else if (character >= 32) escaped += "\\u" + std::to_string(static_cast<short>(character)) + "?";
        }
        const std::pair<const char*, const char*> entities[] = {{"&nbsp;", " "}, {"&copy;", "(c)"}, {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}};
        for (const auto& entity : entities) {
            std::string::size_type position = 0;
            while ((position = escaped.find(entity.first, position)) != std::string::npos) {
                escaped.replace(position, std::strlen(entity.first), entity.second);
                position += std::strlen(entity.second);
            }
        }
        return escaped;
    };
    auto flush = [&] { if (!text.empty()) { result += escape(text) + "\\par "; text.clear(); } };
    for (size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '<') {
            flush(); std::string tag; ++i;
            while (i < source.size() && source[i] != '>') tag += static_cast<char>(std::tolower(static_cast<unsigned char>(source[i++])));
            const bool closing = !tag.empty() && tag[0] == '/';
            const size_t name_start = closing ? 1 : 0;
            const size_t name_end = tag.find_first_of(" \t/", name_start);
            const std::string name = tag.substr(name_start, name_end == std::string::npos ? std::string::npos : name_end - name_start);
            if (name == "head" || name == "style" || name == "script" || name == "noscript") hidden = !closing;
            if (!closing && name == "h1") result += "\\f1\\fs40\\b ";
            if (!closing && name == "h2") result += "\\f1\\fs32\\b ";
            if (!closing && name == "h3") result += "\\f1\\fs28\\b ";
            if (closing && (name == "h1" || name == "h2" || name == "h3")) result += "\\b0\\f0\\fs24 ";
            if (!closing && name == "a") {
                active_link.clear();
                const size_t href = tag.find("href=");
                if (href != std::string::npos) {
                    size_t value_start = href + 5;
                    while (value_start < tag.size() && (tag[value_start] == ' ' || tag[value_start] == '\t')) ++value_start;
                    if (value_start < tag.size() && (tag[value_start] == '\"' || tag[value_start] == '\'')) {
                        const char quote = tag[value_start++];
                        const size_t value_end = tag.find(quote, value_start);
                        active_link = tag.substr(value_start, value_end == std::string::npos ? std::string::npos : value_end - value_start);
                    } else {
                        const size_t value_end = tag.find_first_of(" \t", value_start);
                        active_link = tag.substr(value_start, value_end == std::string::npos ? std::string::npos : value_end - value_start);
                    }
                }
                result += "\\ul\\cf1 ";
            }
            if (closing && name == "a") {
                if (!active_link.empty()) result += "  [" + escape(active_link) + "] ";
                result += "\\ul0\\cf0 ";
                active_link.clear();
            }
            if (name == "p" || name == "br" || name == "div" || name == "li" || name == "pre" || name == "section" || name == "article" || name == "header" || name == "footer") flush();
        } else if (!hidden) text += source[i];
    }
    flush();
    if (result.size() <= 108) return "No readable text found on this page.";
    result += "}";
    return result;
}
}

extern "C" char* browsy_navigate(const char* raw_url) {
    const std::wstring url = to_wide(raw_url ? raw_url : "");
    std::wstring error;
    const bool bezcp = _wcsnicmp(url.c_str(), L"bezcp://", 8) == 0;
    const std::string body = url == L"http://browsybrowser.local" ? "<h1>Welcome to browsybrowser</h1><p>Rust UI. C++ core. Python pages.</p><p>&lt;script&gt; is inert. &lt;py&gt;print(2 + 2)&lt;/py&gt; runs Python.</p>" : fetch(url, error);
    const std::string output = body.empty() && !error.empty() ? to_utf8(L"Page unavailable\n\n" + error) : (bezcp ? run_file(body, "python", ".py") : render(body));
    char* result = static_cast<char*>(std::malloc(output.size() + 1));
    if (!result) return nullptr;
    std::memcpy(result, output.data(), output.size()); result[output.size()] = '\0';
    return result;
}

extern "C" char* browsy_render_html(const char* raw_html) {
    const std::string output = render(raw_html ? raw_html : "");
    char* result = static_cast<char*>(std::malloc(output.size() + 1));
    if (!result) return nullptr;
    std::memcpy(result, output.data(), output.size()); result[output.size()] = '\0';
    return result;
}

extern "C" void browsy_free(char* value) { std::free(value); }