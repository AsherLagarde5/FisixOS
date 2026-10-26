#pragma once
#include "engine_dom.h"
#include <string>
#include <vector>
#include <memory>
#include <stack>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace engine {

class HTMLParser {
public:
    static std::shared_ptr<DOMDocument> parse(const std::wstring& raw_html, const std::wstring& document_url = L"") {
        auto doc = std::make_shared<DOMDocument>();
        doc->url = document_url;

        std::wstring html = preprocess_scripts(raw_html);

        std::stack<std::shared_ptr<DOMNode>> node_stack;
        node_stack.push(doc);

        size_t cursor = 0;
        size_t len = html.size();

        while (cursor < len) {
            if (html[cursor] == L'<' && cursor + 1 < len) {
                // Check for comment <!-- ... -->
                if (cursor + 3 < len && html.compare(cursor, 4, L"<!--") == 0) {
                    size_t end_comment = html.find(L"-->", cursor + 4);
                    if (end_comment == std::wstring::npos) break;
                    cursor = end_comment + 3;
                    continue;
                }

                // Check for <!DOCTYPE ...> or other <! directives
                if (cursor + 1 < len && html[cursor + 1] == L'!') {
                    size_t end_dir = html.find(L'>', cursor + 2);
                    if (end_dir == std::wstring::npos) break;
                    cursor = end_dir + 1;
                    continue;
                }

                // Check for closing tag </tag>
                if (html[cursor + 1] == L'/') {
                    size_t tag_end = html.find(L'>', cursor + 2);
                    if (tag_end == std::wstring::npos) break;
                    std::wstring tag_name = to_lower(trim(html.substr(cursor + 2, tag_end - (cursor + 2))));
                    cursor = tag_end + 1;

                    // Pop matching tag from stack
                    if (node_stack.size() > 1) {
                        auto current = node_stack.top();
                        if (current->type == NodeType::Element) {
                            auto elem = std::static_pointer_cast<DOMElement>(current);
                            if (elem->tag_name == tag_name) {
                                node_stack.pop();
                            } else {
                                // Search down stack for matching element
                                std::vector<std::shared_ptr<DOMNode>> popped;
                                while (node_stack.size() > 1) {
                                    auto top_elem = node_stack.top();
                                    node_stack.pop();
                                    if (top_elem->type == NodeType::Element &&
                                        std::static_pointer_cast<DOMElement>(top_elem)->tag_name == tag_name) {
                                        break;
                                    }
                                    popped.push_back(top_elem);
                                }
                            }
                        }
                    }
                    continue;
                }

                // Opening or Self-closing tag <tag ...>
                size_t tag_end = html.find(L'>', cursor + 1);
                if (tag_end == std::wstring::npos) break;

                std::wstring tag_content = html.substr(cursor + 1, tag_end - (cursor + 1));
                cursor = tag_end + 1;

                bool is_self_closing = false;
                if (!tag_content.empty() && tag_content.back() == L'/') {
                    is_self_closing = true;
                    tag_content.pop_back();
                }

                // Parse tag name and attributes
                std::wstring tag_name;
                std::map<std::wstring, std::wstring> attributes;
                parse_tag_content(tag_content, tag_name, attributes);

                if (tag_name.empty()) continue;

                // Handle special tags: <style>, <title>, <script>, <noscript>
                if (tag_name == L"style") {
                    size_t close_style = html.find(L"</style>", cursor);
                    if (close_style != std::wstring::npos) {
                        std::wstring css_content = html.substr(cursor, close_style - cursor);
                        doc->embedded_stylesheets.push_back(css_content);
                        cursor = close_style + 8;
                    }
                    continue;
                } else if (tag_name == L"title") {
                    size_t close_title = html.find(L"</title>", cursor);
                    if (close_title != std::wstring::npos) {
                        doc->title = decode_html_entities(trim(html.substr(cursor, close_title - cursor)));
                        cursor = close_title + 8;
                    }
                    continue;
                } else if (tag_name == L"script") {
                    size_t close_script = html.find(L"</script>", cursor);
                    if (close_script != std::wstring::npos) {
                        cursor = close_script + 9;
                    } else {
                        cursor = len;
                    }
                    continue;
                } else if (tag_name == L"noscript") {
                    size_t close_ns = html.find(L"</noscript>", cursor);
                    if (close_ns != std::wstring::npos) {
                        cursor = close_ns + 11;
                    } else {
                        cursor = len;
                    }
                    continue;
                } else if (tag_name == L"link" &&
                           to_lower(attributes[L"rel"]).find(L"stylesheet") != std::wstring::npos) {
                    const auto href = attributes.find(L"href");
                    if (href != attributes.end() && !href->second.empty()) {
                        doc->stylesheet_urls.push_back(href->second);
                    }
                }

                // Auto-close preceding optional tags per HTML5 tree construction rules
                if (tag_name == L"p" || tag_name == L"div" || tag_name == L"h1" || tag_name == L"h2" || tag_name == L"h3" || tag_name == L"h4" || tag_name == L"ul" || tag_name == L"ol" || tag_name == L"table") {
                    if (node_stack.size() > 1 && node_stack.top()->type == NodeType::Element &&
                        std::static_pointer_cast<DOMElement>(node_stack.top())->tag_name == L"p") {
                        node_stack.pop();
                    }
                }
                if (tag_name == L"li") {
                    if (node_stack.size() > 1 && node_stack.top()->type == NodeType::Element &&
                        std::static_pointer_cast<DOMElement>(node_stack.top())->tag_name == L"li") {
                        node_stack.pop();
                    }
                }
                if (tag_name == L"td" || tag_name == L"th") {
                    if (node_stack.size() > 1 && node_stack.top()->type == NodeType::Element) {
                        auto cur_tag = std::static_pointer_cast<DOMElement>(node_stack.top())->tag_name;
                        if (cur_tag == L"td" || cur_tag == L"th") {
                            node_stack.pop();
                        }
                    }
                }
                if (tag_name == L"tr") {
                    while (node_stack.size() > 1 && node_stack.top()->type == NodeType::Element) {
                        auto cur_tag = std::static_pointer_cast<DOMElement>(node_stack.top())->tag_name;
                        if (cur_tag == L"td" || cur_tag == L"th" || cur_tag == L"tr") {
                            node_stack.pop();
                        } else {
                            break;
                        }
                    }
                }

                auto elem = std::make_shared<DOMElement>(tag_name);
                elem->attributes = attributes;
                elem->parent = node_stack.top().get();
                node_stack.top()->children.push_back(elem);

                // Void tags that don't take closing tags
                static const std::vector<std::wstring> void_tags = {
                    L"area", L"base", L"br", L"col", L"embed", L"hr",
                    L"img", L"input", L"link", L"meta", L"param",
                    L"source", L"track", L"wbr"
                };

                bool is_void = is_self_closing ||
                    (std::find(void_tags.begin(), void_tags.end(), tag_name) != void_tags.end());

                if (!is_void) {
                    node_stack.push(elem);
                }
            } else {
                // Text node
                size_t next_tag = html.find(L'<', cursor);
                if (next_tag == std::wstring::npos) next_tag = len;

                std::wstring raw_text = html.substr(cursor, next_tag - cursor);
                cursor = next_tag;

                std::wstring decoded = decode_html_entities(raw_text);
                // Collapse whitespaces unless inside <pre>
                bool is_pre = false;
                if (!node_stack.empty() && node_stack.top()->type == NodeType::Element) {
                    is_pre = (std::static_pointer_cast<DOMElement>(node_stack.top())->tag_name == L"pre");
                }

                std::wstring cleaned_text;
                if (is_pre) {
                    cleaned_text = decoded;
                } else {
                    bool in_space = false;
                    for (wchar_t c : decoded) {
                        if (iswspace(c)) {
                            if (!in_space) {
                                cleaned_text += L' ';
                                in_space = true;
                            }
                        } else {
                            cleaned_text += c;
                            in_space = false;
                        }
                    }
                }

                if (!cleaned_text.empty() && (is_pre || cleaned_text != L" ")) {
                    auto text_node = std::make_shared<DOMText>(cleaned_text);
                    text_node->parent = node_stack.top().get();
                    node_stack.top()->children.push_back(text_node);
                }
            }
        }

        return doc;
    }

private:
    static void parse_tag_content(const std::wstring& content, std::wstring& out_tag_name, std::map<std::wstring, std::wstring>& out_attrs) {
        std::wstring s = trim(content);
        if (s.empty()) return;

        size_t name_end = s.find_first_of(L" \t\r\n/");
        if (name_end == std::wstring::npos) {
            out_tag_name = to_lower(s);
            return;
        }

        out_tag_name = to_lower(s.substr(0, name_end));
        size_t cursor = name_end;

        while (cursor < s.size()) {
            while (cursor < s.size() && (iswspace(s[cursor]) || s[cursor] == L'/')) ++cursor;
            if (cursor >= s.size()) break;

            size_t attr_name_end = s.find_first_of(L"= \t\r\n/", cursor);
            if (attr_name_end == std::wstring::npos) attr_name_end = s.size();

            std::wstring attr_name = to_lower(s.substr(cursor, attr_name_end - cursor));
            cursor = attr_name_end;

            while (cursor < s.size() && iswspace(s[cursor])) ++cursor;

            std::wstring attr_val;
            if (cursor < s.size() && s[cursor] == L'=') {
                ++cursor;
                while (cursor < s.size() && iswspace(s[cursor])) ++cursor;
                if (cursor < s.size()) {
                    if (s[cursor] == L'\"' || s[cursor] == L'\'') {
                        wchar_t quote = s[cursor++];
                        size_t val_end = s.find(quote, cursor);
                        if (val_end == std::wstring::npos) val_end = s.size();
                        attr_val = s.substr(cursor, val_end - cursor);
                        cursor = val_end + 1;
                    } else {
                        size_t val_end = s.find_first_of(L" \t\r\n/>", cursor);
                        if (val_end == std::wstring::npos) val_end = s.size();
                        attr_val = s.substr(cursor, val_end - cursor);
                        cursor = val_end;
                    }
                }
            }
            if (!attr_name.empty()) {
                out_attrs[attr_name] = decode_html_entities(attr_val);
            }
        }
    }

    static std::wstring preprocess_scripts(const std::wstring& html) {
        std::wstring result;
        size_t cursor = 0;
        size_t len = html.size();

        while (cursor < len) {
            size_t py_pos = html.find(L"<py>", cursor);
            if (py_pos == std::wstring::npos) {
                result += html.substr(cursor);
                break;
            }

            result += html.substr(cursor, py_pos - cursor);
            size_t py_end = html.find(L"</py>", py_pos + 4);
            if (py_end == std::wstring::npos) {
                result += L"<p>Unclosed &lt;py&gt; tag.</p>";
                break;
            }

            std::wstring py_code = html.substr(py_pos + 4, py_end - (py_pos + 4));
            std::wstring output = run_python_snippet(py_code);
            result += L"<pre class=\"py-output\">" + output + L"</pre>";
            cursor = py_end + 5;
        }

        return result;
    }

    static std::wstring run_python_snippet(const std::wstring& code) {
        std::string utf8_code;
        int len = WideCharToMultiByte(CP_UTF8, 0, code.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            utf8_code.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, code.c_str(), -1, utf8_code.data(), len, nullptr, nullptr);
        }

        auto temp_path = std::filesystem::temp_directory_path() / ("browsy_py_" + std::to_string(GetCurrentProcessId()) + ".py");
        {
            std::ofstream out(temp_path, std::ios::binary);
            out << utf8_code;
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
