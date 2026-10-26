#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <filesystem>
#include "internal_pages.h"

#pragma comment(lib, "winhttp.lib")

namespace engine {

struct NetworkResponse {
    bool success = false;
    int status_code = 0;
    std::wstring final_url;
    std::wstring content_type;
    std::string body_bytes;
    std::wstring body_text;
    std::wstring error_message;
};

class NetworkClient {
public:
    static NetworkResponse fetch(const std::wstring& url, int max_redirects = 5) {
        NetworkResponse resp;
        resp.final_url = url;

        // 1. Handle browsy:// Internal Protocol
        if (url.rfind(L"browsy://", 0) == 0 || url == L"about:blank") {
            resp.success = true;
            resp.status_code = 200;
            resp.content_type = L"text/html";
            std::string html_utf8 = internal_pages::get_page_content(url);
            resp.body_bytes = html_utf8;

            int len = MultiByteToWideChar(CP_UTF8, 0, html_utf8.c_str(), -1, nullptr, 0);
            if (len > 0) {
                resp.body_text.resize(len - 1);
                MultiByteToWideChar(CP_UTF8, 0, html_utf8.c_str(), -1, resp.body_text.data(), len);
            }
            return resp;
        }

        // 2. Handle file:// Local Files
        if (url.rfind(L"file://", 0) == 0) {
            std::wstring file_path = url.substr(7);
            if (file_path.size() >= 3 && file_path[0] == L'/' && file_path[2] == L':') {
                file_path = file_path.substr(1);
            }
            std::ifstream file(file_path, std::ios::binary);
            if (file) {
                resp.body_bytes = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                resp.success = true;
                resp.status_code = 200;
                resp.content_type = L"text/html";

                int len = MultiByteToWideChar(CP_UTF8, 0, resp.body_bytes.c_str(), -1, nullptr, 0);
                if (len > 0) {
                    resp.body_text.resize(len - 1);
                    MultiByteToWideChar(CP_UTF8, 0, resp.body_bytes.c_str(), -1, resp.body_text.data(), len);
                }
            } else {
                resp.error_message = L"File not found: " + file_path;
            }
            return resp;
        }

        // 3. Handle bezcp:// Protocol (HTTP GET + Local Python Execution)
        bool is_bezcp = (url.rfind(L"bezcp://", 0) == 0);
        std::wstring fetch_url = url;
        if (is_bezcp) {
            fetch_url = L"http://" + url.substr(8);
        }

        // 4. Handle HTTP / HTTPS with WinHTTP
        bool is_https = (fetch_url.rfind(L"https://", 0) == 0);
        std::wstring prefix = is_https ? L"https://" : L"http://";
        if (fetch_url.rfind(prefix, 0) != 0) {
            resp.error_message = L"Unsupported protocol: " + url;
            return resp;
        }

        std::wstring address = fetch_url.substr(prefix.size());
        size_t slash = address.find(L'/');
        std::wstring host = (slash == std::wstring::npos) ? address : address.substr(0, slash);
        std::wstring path = (slash == std::wstring::npos) ? L"/" : address.substr(slash);

        // Parse optional port
        INTERNET_PORT port = is_https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
        size_t colon = host.find(L':');
        if (colon != std::wstring::npos) {
            try {
                port = static_cast<INTERNET_PORT>(std::stoi(host.substr(colon + 1)));
            } catch (...) {}
            host = host.substr(0, colon);
        }

        HINTERNET session = WinHttpOpen(
            L"browsybrowser/1.0 (from scratch; Windows x64)",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );

        if (!session) {
            resp.error_message = L"Failed to initialize network session.";
            return resp;
        }

        HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
        if (!connect) {
            WinHttpCloseHandle(session);
            resp.error_message = L"Could not connect to host: " + host;
            return resp;
        }

        DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET request = WinHttpOpenRequest(
            connect,
            L"GET",
            path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags
        );

        if (!request) {
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            resp.error_message = L"Failed to create HTTP request.";
            return resp;
        }

        // Enable automatic redirect handling
        DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));

        BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (sent) {
            sent = WinHttpReceiveResponse(request, nullptr);
        }

        if (sent) {
            DWORD status = 0;
            DWORD status_size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
            resp.status_code = status;

            // Query Content-Type
            DWORD ct_len = 0;
            WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
                nullptr, &ct_len, WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && ct_len > 0) {
                std::wstring ct_buf(ct_len / sizeof(wchar_t), L'\0');
                if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
                    ct_buf.data(), &ct_len, WINHTTP_NO_HEADER_INDEX)) {
                    resp.content_type = ct_buf;
                }
            }

            // Read response body data
            char buffer[8192];
            DWORD bytes_read = 0;
            do {
                if (WinHttpReadData(request, buffer, sizeof(buffer), &bytes_read) && bytes_read > 0) {
                    resp.body_bytes.append(buffer, bytes_read);
                }
            } while (bytes_read > 0);

            resp.success = true;

            if (is_bezcp) {
                // Execute received code as Python
                resp.body_text = run_python_source(resp.body_bytes);
            } else {
                // Decode UTF-8 to wide string
                int wlen = MultiByteToWideChar(CP_UTF8, 0, resp.body_bytes.c_str(), (int)resp.body_bytes.size(), nullptr, 0);
                if (wlen > 0) {
                    resp.body_text.resize(wlen);
                    MultiByteToWideChar(CP_UTF8, 0, resp.body_bytes.c_str(), (int)resp.body_bytes.size(), resp.body_text.data(), wlen);
                }
            }
        } else {
            resp.error_message = L"Unable to load " + url + L" (Error: " + std::to_wstring(GetLastError()) + L")";
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);

        return resp;
    }

private:
    static std::wstring run_python_source(const std::string& py_code) {
        auto temp_path = std::filesystem::temp_directory_path() / ("bezcp_" + std::to_string(GetCurrentProcessId()) + ".py");
        {
            std::ofstream out(temp_path, std::ios::binary);
            out << py_code;
        }

        std::string cmd = "python \"" + temp_path.string() + "\" 2>&1";
        FILE* pipe = _popen(cmd.c_str(), "r");
        std::string raw_out;
        if (pipe) {
            char buf[512];
            while (fgets(buf, sizeof(buf), pipe)) raw_out += buf;
            _pclose(pipe);
        }
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);

        int wlen = MultiByteToWideChar(CP_UTF8, 0, raw_out.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return L"";
        std::wstring wout(wlen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, raw_out.c_str(), -1, wout.data(), wlen);
        return wout;
    }
};

} // namespace engine
