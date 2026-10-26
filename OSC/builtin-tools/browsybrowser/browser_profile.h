#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>

struct BookmarkItem {
    std::wstring title;
    std::wstring url;
    int64_t timestamp = 0;
};

struct HistoryItem {
    std::wstring title;
    std::wstring url;
    int64_t timestamp = 0;
};

struct BrowserSettings {
    std::wstring search_engine = L"google";
    std::wstring homepage = L"browsy://newtab";
    bool show_bookmarks_bar = true;
    std::wstring theme = L"obsidian_teal";
};

class ProfileManager {
public:
    std::vector<BookmarkItem> bookmarks;
    std::vector<HistoryItem> history;
    BrowserSettings settings;
    std::vector<std::pair<std::wstring, std::wstring>> closed_tabs;

    static ProfileManager& instance() {
        static ProfileManager s_instance;
        return s_instance;
    }

    void init() {
        load();
        if (bookmarks.empty()) {
            populate_defaults();
            save();
        }
    }

    std::wstring get_search_url(const std::wstring& query) const {
        std::wstring encoded = url_encode(query);
        if (settings.search_engine == L"duckduckgo") {
            return L"https://duckduckgo.com/?q=" + encoded;
        } else if (settings.search_engine == L"bing") {
            return L"https://www.bing.com/search?q=" + encoded;
        } else if (settings.search_engine == L"brave") {
            return L"https://search.brave.com/search?q=" + encoded;
        } else if (settings.search_engine == L"ecosia") {
            return L"https://www.ecosia.org/search?q=" + encoded;
        }
        return L"https://www.google.com/search?q=" + encoded;
    }

    bool is_bookmarked(const std::wstring& url) const {
        for (const auto& b : bookmarks) {
            if (_wcsicmp(b.url.c_str(), url.c_str()) == 0) return true;
        }
        return false;
    }

    void toggle_bookmark(const std::wstring& title, const std::wstring& url) {
        auto it = std::find_if(bookmarks.begin(), bookmarks.end(), [&](const BookmarkItem& b) {
            return _wcsicmp(b.url.c_str(), url.c_str()) == 0;
        });
        if (it != bookmarks.end()) {
            bookmarks.erase(it);
        } else {
            BookmarkItem item;
            item.title = title.empty() ? url : title;
            item.url = url;
            item.timestamp = current_timestamp();
            bookmarks.push_back(item);
        }
        save();
    }

    void add_bookmark(const std::wstring& title, const std::wstring& url) {
        if (is_bookmarked(url)) return;
        BookmarkItem item;
        item.title = title.empty() ? url : title;
        item.url = url;
        item.timestamp = current_timestamp();
        bookmarks.push_back(item);
        save();
    }

    void remove_bookmark(const std::wstring& url) {
        auto it = std::find_if(bookmarks.begin(), bookmarks.end(), [&](const BookmarkItem& b) {
            return _wcsicmp(b.url.c_str(), url.c_str()) == 0;
        });
        if (it != bookmarks.end()) {
            bookmarks.erase(it);
            save();
        }
    }

    void add_history(const std::wstring& title, const std::wstring& url) {
        if (url.empty() || url.rfind(L"browsy://", 0) == 0 || url == L"about:blank") return;
        if (!history.empty() && history.front().url == url) {
            history.front().title = title.empty() ? url : title;
            history.front().timestamp = current_timestamp();
            save();
            return;
        }
        HistoryItem item;
        item.title = title.empty() ? url : title;
        item.url = url;
        item.timestamp = current_timestamp();
        history.insert(history.begin(), item);
        if (history.size() > 2000) history.pop_back();
        save();
    }

    void clear_history() {
        history.clear();
        save();
    }

    void push_closed_tab(const std::wstring& url, const std::wstring& title) {
        if (url.empty() || url == L"about:blank" || url == L"browsy://newtab") return;
        closed_tabs.push_back({url, title});
        if (closed_tabs.size() > 20) closed_tabs.erase(closed_tabs.begin());
    }

    bool pop_closed_tab(std::wstring& out_url, std::wstring& out_title) {
        if (closed_tabs.empty()) return false;
        out_url = closed_tabs.back().first;
        out_title = closed_tabs.back().second;
        closed_tabs.pop_back();
        return true;
    }

    void save() {
        std::wstring path = get_profile_path();
        std::ofstream out(path, std::ios::binary);
        if (!out) return;

        out << "{\n";
        out << "  \"settings\": {\n";
        out << "    \"searchEngine\": \"" << escape_json(settings.search_engine) << "\",\n";
        out << "    \"homepage\": \"" << escape_json(settings.homepage) << "\",\n";
        out << "    \"showBookmarksBar\": " << (settings.show_bookmarks_bar ? "true" : "false") << ",\n";
        out << "    \"theme\": \"" << escape_json(settings.theme) << "\"\n";
        out << "  },\n";

        out << "  \"bookmarks\": [\n";
        for (size_t i = 0; i < bookmarks.size(); ++i) {
            out << "    {\n";
            out << "      \"title\": \"" << escape_json(bookmarks[i].title) << "\",\n";
            out << "      \"url\": \"" << escape_json(bookmarks[i].url) << "\",\n";
            out << "      \"timestamp\": " << bookmarks[i].timestamp << "\n";
            out << "    }" << (i + 1 < bookmarks.size() ? "," : "") << "\n";
        }
        out << "  ],\n";

        out << "  \"history\": [\n";
        for (size_t i = 0; i < history.size(); ++i) {
            out << "    {\n";
            out << "      \"title\": \"" << escape_json(history[i].title) << "\",\n";
            out << "      \"url\": \"" << escape_json(history[i].url) << "\",\n";
            out << "      \"timestamp\": " << history[i].timestamp << "\n";
            out << "    }" << (i + 1 < history.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
    }

    void load() {
        std::wstring path = get_profile_path();
        std::ifstream in(path, std::ios::binary);
        if (!in) return;

        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        bookmarks.clear();
        history.clear();

        auto extract_string = [&](const std::string& key, std::wstring& target) {
            size_t pos = content.find("\"" + key + "\"");
            if (pos != std::string::npos) {
                size_t colon = content.find(':', pos);
                size_t start_q = content.find('\"', colon + 1);
                if (start_q != std::string::npos) {
                    size_t end_q = content.find('\"', start_q + 1);
                    while (end_q != std::string::npos && content[end_q - 1] == '\\') {
                        end_q = content.find('\"', end_q + 1);
                    }
                    if (end_q != std::string::npos) {
                        target = unescape_json(content.substr(start_q + 1, end_q - start_q - 1));
                    }
                }
            }
        };

        auto extract_bool = [&](const std::string& key, bool& target) {
            size_t pos = content.find("\"" + key + "\"");
            if (pos != std::string::npos) {
                size_t colon = content.find(':', pos);
                if (colon != std::string::npos) {
                    size_t val_start = content.find_first_not_of(" \t\r\n", colon + 1);
                    if (val_start != std::string::npos) {
                        if (content.compare(val_start, 4, "true") == 0) target = true;
                        else if (content.compare(val_start, 5, "false") == 0) target = false;
                    }
                }
            }
        };

        extract_string("searchEngine", settings.search_engine);
        extract_string("homepage", settings.homepage);
        extract_bool("showBookmarksBar", settings.show_bookmarks_bar);
        extract_string("theme", settings.theme);

        auto extract_items = [&](const std::string& array_name, auto& list) {
            size_t arr_pos = content.find("\"" + array_name + "\"");
            if (arr_pos == std::string::npos) return;
            size_t start_bracket = content.find('[', arr_pos);
            size_t end_bracket = content.find(']', start_bracket);
            if (start_bracket == std::string::npos || end_bracket == std::string::npos) return;

            std::string arr_str = content.substr(start_bracket, end_bracket - start_bracket + 1);
            size_t cursor = 0;
            while (cursor < arr_str.size()) {
                size_t obj_start = arr_str.find('{', cursor);
                if (obj_start == std::string::npos) break;
                size_t obj_end = arr_str.find('}', obj_start);
                if (obj_end == std::string::npos) break;

                std::string obj_str = arr_str.substr(obj_start, obj_end - obj_start + 1);
                std::wstring item_title, item_url;
                int64_t item_ts = 0;

                size_t t_pos = obj_str.find("\"title\"");
                if (t_pos != std::string::npos) {
                    size_t q1 = obj_str.find('\"', obj_str.find(':', t_pos) + 1);
                    size_t q2 = obj_str.find('\"', q1 + 1);
                    while (q2 != std::string::npos && obj_str[q2 - 1] == '\\') q2 = obj_str.find('\"', q2 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos) item_title = unescape_json(obj_str.substr(q1 + 1, q2 - q1 - 1));
                }

                size_t u_pos = obj_str.find("\"url\"");
                if (u_pos != std::string::npos) {
                    size_t q1 = obj_str.find('\"', obj_str.find(':', u_pos) + 1);
                    size_t q2 = obj_str.find('\"', q1 + 1);
                    while (q2 != std::string::npos && obj_str[q2 - 1] == '\\') q2 = obj_str.find('\"', q2 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos) item_url = unescape_json(obj_str.substr(q1 + 1, q2 - q1 - 1));
                }

                size_t ts_pos = obj_str.find("\"timestamp\"");
                if (ts_pos != std::string::npos) {
                    size_t col = obj_str.find(':', ts_pos);
                    if (col != std::string::npos) {
                        try { item_ts = std::stoll(obj_str.substr(col + 1)); } catch (...) {}
                    }
                }

                if (!item_url.empty()) {
                    list.push_back({item_title, item_url, item_ts});
                }
                cursor = obj_end + 1;
            }
        };

        extract_items("bookmarks", bookmarks);
        extract_items("history", history);
    }

private:
    static int64_t current_timestamp() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    static std::wstring get_profile_path() {
        PWSTR local_app_data = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_app_data))) {
            std::wstring path = local_app_data;
            CoTaskMemFree(local_app_data);
            path += L"\\browsybrowser";
            CreateDirectoryW(path.c_str(), nullptr);
            return path + L"\\profile.json";
        }
        return L"profile.json";
    }

    void populate_defaults() {
        bookmarks = {
            { L"Google", L"https://www.google.com", current_timestamp() },
            { L"YouTube", L"https://www.youtube.com", current_timestamp() },
            { L"GitHub", L"https://github.com", current_timestamp() },
            { L"Reddit", L"https://www.reddit.com", current_timestamp() },
            { L"Wikipedia", L"https://www.wikipedia.org", current_timestamp() },
            { L"ChatGPT", L"https://chatgpt.com", current_timestamp() },
            { L"Hacker News", L"https://news.ycombinator.com", current_timestamp() }
        };
    }

    static std::string escape_json(const std::wstring& s) {
        std::string res;
        for (wchar_t c : s) {
            if (c == L'\"') res += "\\\"";
            else if (c == L'\\') res += "\\\\";
            else if (c == L'\b') res += "\\b";
            else if (c == L'\f') res += "\\f";
            else if (c == L'\n') res += "\\n";
            else if (c == L'\r') res += "\\r";
            else if (c == L'\t') res += "\\t";
            else if (c < 32 || c > 126) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                res += buf;
            } else {
                res += static_cast<char>(c);
            }
        }
        return res;
    }

    static std::wstring unescape_json(const std::string& s) {
        std::wstring res;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char next = s[++i];
                if (next == '\"') res += L'\"';
                else if (next == '\\') res += L'\\';
                else if (next == 'n') res += L'\n';
                else if (next == 'r') res += L'\r';
                else if (next == 't') res += L'\t';
                else if (next == 'u' && i + 4 < s.size()) {
                    std::string hex = s.substr(i + 1, 4);
                    wchar_t code = static_cast<wchar_t>(std::stoul(hex, nullptr, 16));
                    res += code;
                    i += 4;
                } else res += static_cast<wchar_t>(next);
            } else {
                res += static_cast<wchar_t>(static_cast<unsigned char>(s[i]));
            }
        }
        return res;
    }

    static std::wstring url_encode(const std::wstring& value) {
        std::wostringstream escaped;
        escaped.fill(L'0');
        escaped << std::hex;
        for (wchar_t c : value) {
            if (iswalnum(c) || c == L'-' || c == L'_' || c == L'.' || c == L'~') {
                escaped << c;
            } else if (c == L' ') {
                escaped << L'+';
            } else if (c < 128) {
                escaped << L'%' << std::setw(2) << std::uppercase << int(static_cast<unsigned char>(c));
            } else {
                char utf8[8] = {};
                int len = WideCharToMultiByte(CP_UTF8, 0, &c, 1, utf8, sizeof(utf8), nullptr, nullptr);
                for (int i = 0; i < len; ++i) {
                    escaped << L'%' << std::setw(2) << std::uppercase << int(static_cast<unsigned char>(utf8[i]));
                }
            }
        }
        return escaped.str();
    }
};
