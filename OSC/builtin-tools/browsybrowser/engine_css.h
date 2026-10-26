#pragma once
#include "engine_dom.h"
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>

namespace engine {

struct ComputedStyle {
    Display display = Display::Block;
    Color color = Color(220, 226, 235);
    Color background_color = Color::transparent();
    int font_size = 15;
    int font_weight = FW_NORMAL;
    std::wstring font_family = L"Segoe UI";
    bool is_italic = false;
    bool is_underline = false;
    TextAlign text_align = TextAlign::Left;

    EdgeInsets margin = { 0, 0, 0, 0 };
    EdgeInsets padding = { 0, 0, 0, 0 };
    int border_width = 0;
    Color border_color = Color::transparent();
    int border_radius = 0;

    int width = -1;  // -1 = auto
    int max_width = -1;
    int height = -1; // -1 = auto
    float width_percent = -1.0f;
    float max_width_percent = -1.0f;
    int line_height = 22;

    float opacity = 1.0f;  // CSS opacity (compositor layer trigger when < 1)
    int z_index = 0;       // CSS z-index (stacking context / layer promotion)

    bool is_clickable = false;
    std::wstring href;
};

struct CSSSelector {
    std::wstring tag;
    std::wstring id;
    std::vector<std::wstring> classes;
    std::wstring pseudo;
    int specificity = 0;

    bool matches(const DOMElement& elem) const {
        if (!tag.empty() && tag != L"*" && tag != elem.tag_name) return false;
        if (!id.empty() && id != elem.id()) return false;
        for (const auto& cls : classes) {
            if (!elem.has_class(cls)) return false;
        }
        return true;
    }
};

struct CSSDeclaration {
    std::wstring property;
    std::wstring value;
};

struct CSSRule {
    std::vector<CSSSelector> selectors;
    std::vector<CSSDeclaration> declarations;
};

class StyleSheet {
public:
    std::vector<CSSRule> rules;

    static StyleSheet parse(const std::wstring& css_text) {
        StyleSheet sheet;
        size_t cursor = 0;
        size_t len = css_text.size();

        while (cursor < len) {
            // Skip comments /* ... */
            if (cursor + 1 < len && css_text[cursor] == L'/' && css_text[cursor + 1] == L'*') {
                size_t end_comment = css_text.find(L"*/", cursor + 2);
                if (end_comment == std::wstring::npos) break;
                cursor = end_comment + 2;
                continue;
            }

            size_t open_brace = css_text.find(L'{', cursor);
            if (open_brace == std::wstring::npos) break;

            std::wstring selectors_str = trim(css_text.substr(cursor, open_brace - cursor));
            size_t close_brace = css_text.find(L'}', open_brace + 1);
            if (close_brace == std::wstring::npos) break;

            std::wstring body_str = css_text.substr(open_brace + 1, close_brace - (open_brace + 1));
            cursor = close_brace + 1;

            if (selectors_str.empty() || selectors_str[0] == L'@') continue;

            CSSRule rule;
            std::wstringstream ss(selectors_str);
            std::wstring sel_part;
            while (std::getline(ss, sel_part, L',')) {
                CSSSelector sel = parse_selector(trim(sel_part));
                if (!sel.tag.empty() || !sel.id.empty() || !sel.classes.empty()) {
                    rule.selectors.push_back(sel);
                }
            }

            rule.declarations = parse_declarations(body_str);
            if (!rule.selectors.empty() && !rule.declarations.empty()) {
                sheet.rules.push_back(rule);
            }
        }

        return sheet;
    }

    static std::vector<CSSDeclaration> parse_declarations(const std::wstring& decls_str) {
        std::vector<CSSDeclaration> list;
        std::wstringstream ss(decls_str);
        std::wstring decl;
        while (std::getline(ss, decl, L';')) {
            std::wstring d = trim(decl);
            size_t colon = d.find(L':');
            if (colon != std::wstring::npos) {
                std::wstring prop = to_lower(trim(d.substr(0, colon)));
                std::wstring val = trim(d.substr(colon + 1));
                if (!prop.empty() && !val.empty()) {
                    list.push_back({ prop, val });
                }
            }
        }
        return list;
    }

private:
    static CSSSelector parse_selector(const std::wstring& s) {
        CSSSelector sel;
        std::wstring str = s;
        size_t colon = str.find(L':');
        if (colon != std::wstring::npos) {
            sel.pseudo = to_lower(str.substr(colon + 1));
            str = str.substr(0, colon);
        }

        size_t hash = str.find(L'#');
        if (hash != std::wstring::npos) {
            size_t end_id = str.find_first_of(L". ", hash);
            if (end_id == std::wstring::npos) end_id = str.size();
            sel.id = str.substr(hash + 1, end_id - (hash + 1));
            str.erase(hash, end_id - hash);
            sel.specificity += 100;
        }

        while (true) {
            size_t dot = str.find(L'.');
            if (dot == std::wstring::npos) break;
            size_t end_cls = str.find_first_of(L". ", dot + 1);
            if (end_cls == std::wstring::npos) end_cls = str.size();
            sel.classes.push_back(to_lower(str.substr(dot + 1, end_cls - (dot + 1))));
            str.erase(dot, end_cls - dot);
            sel.specificity += 10;
        }

        sel.tag = to_lower(trim(str));
        if (!sel.tag.empty() && sel.tag != L"*") sel.specificity += 1;

        return sel;
    }
};

class StyleResolver {
public:
    static ComputedStyle resolve(
        const DOMNode* node,
        const std::vector<StyleSheet>& sheets,
        const ComputedStyle* parent_style = nullptr
    ) {
        ComputedStyle style;

        if (!node) return style;

        if (parent_style) {
            style.color = parent_style->color;
            style.font_size = parent_style->font_size;
            style.font_weight = parent_style->font_weight;
            style.font_family = parent_style->font_family;
            style.is_italic = parent_style->is_italic;
            style.text_align = parent_style->text_align;
            style.line_height = parent_style->line_height;
        }

        if (node->type == NodeType::Document) {
            style.display = Display::Block;
            style.background_color = Color(10, 14, 22);
            style.color = Color(240, 246, 252);
            return style;
        }

        if (node->type == NodeType::Text) {
            style.display = Display::Inline;
            return style;
        }

        const auto* elem = static_cast<const DOMElement*>(node);
        const std::wstring& tag = elem->tag_name;

        // Hidden elements
        if (elem->has_attribute(L"hidden") || elem->get_attribute(L"type") == L"hidden" || elem->get_attribute(L"aria-hidden") == L"true") {
            style.display = Display::None;
            return style;
        }

        // 1. Default User-Agent Stylesheet
        apply_user_agent_defaults(tag, style);

        // Check HTML alignment attributes
        if (elem->get_attribute(L"align") == L"center") {
            style.text_align = TextAlign::Center;
        } else if (elem->get_attribute(L"align") == L"right") {
            style.text_align = TextAlign::Right;
        }

        // Check if <a> tag for href
        if (tag == L"a") {
            style.is_clickable = true;
            style.href = elem->get_attribute(L"href");
            style.color = Color(0, 210, 180);
            style.is_underline = true;
        } else if (tag == L"button") {
            style.is_clickable = true;
        }

        // 2. Match stylesheet rules using specificity, then source order.
        struct CascadedDeclaration {
            CSSDeclaration declaration;
            int specificity = 0;
            size_t order = 0;
        };
        std::map<std::wstring, CascadedDeclaration> cascaded;
        size_t order = 0;
        for (const auto& sheet : sheets) {
            for (const auto& rule : sheet.rules) {
                int matched_specificity = -1;
                for (const auto& sel : rule.selectors) {
                    if (sel.matches(*elem)) {
                        matched_specificity = (std::max)(matched_specificity, sel.specificity);
                    }
                }
                if (matched_specificity < 0) continue;
                for (const auto& declaration : rule.declarations) {
                    auto it = cascaded.find(declaration.property);
                    if (it == cascaded.end() ||
                        matched_specificity > it->second.specificity ||
                        (matched_specificity == it->second.specificity && order >= it->second.order)) {
                        cascaded[declaration.property] = { declaration, matched_specificity, order };
                    }
                    ++order;
                }
            }
        }

        std::vector<CascadedDeclaration> ordered_declarations;
        ordered_declarations.reserve(cascaded.size());
        for (const auto& entry : cascaded) {
            ordered_declarations.push_back(entry.second);
        }
        std::sort(ordered_declarations.begin(), ordered_declarations.end(),
            [](const CascadedDeclaration& left, const CascadedDeclaration& right) {
                return left.order < right.order;
            });

        std::vector<CSSDeclaration> winning_declarations;
        winning_declarations.reserve(cascaded.size());
        for (const auto& entry : ordered_declarations) {
            winning_declarations.push_back(entry.declaration);
        }

        // 3. Inline style="..." Attribute (Highest Specificity)
        std::vector<CSSDeclaration> inline_declarations;
        if (elem->has_attribute(L"style")) {
            inline_declarations = StyleSheet::parse_declarations(elem->get_attribute(L"style"));
        }

        // Resolve font size first so em-based spacing and line heights use the
        // final computed size regardless of declaration order.
        apply_declarations(winning_declarations, style, true);
        apply_declarations(inline_declarations, style, true);
        apply_declarations(winning_declarations, style, false);
        apply_declarations(inline_declarations, style, false);

        return style;
    }

private:
    static void apply_user_agent_defaults(const std::wstring& tag, ComputedStyle& style) {
        if (tag == L"body") {
            style.display = Display::Block;
            style.color = Color(240, 246, 252);
            style.background_color = Color(10, 14, 22);
            style.font_size = 15;
            style.margin = { 8, 8, 8, 8 };
            style.font_family = L"Segoe UI";
            style.line_height = 22;
        } else if (tag == L"center") {
            style.display = Display::Block;
            style.text_align = TextAlign::Center;
        } else if (tag == L"h1") {
            style.display = Display::Block;
            style.font_size = 28;
            style.font_weight = FW_BOLD;
            style.margin = { 16, 0, 10, 0 };
            style.color = Color(255, 255, 255);
        } else if (tag == L"h2") {
            style.display = Display::Block;
            style.font_size = 22;
            style.font_weight = FW_BOLD;
            style.margin = { 14, 0, 8, 0 };
            style.color = Color(255, 255, 255);
        } else if (tag == L"h3") {
            style.display = Display::Block;
            style.font_size = 18;
            style.font_weight = FW_BOLD;
            style.margin = { 12, 0, 6, 0 };
            style.color = Color(255, 255, 255);
        } else if (tag == L"h4") {
            style.display = Display::Block;
            style.font_size = 16;
            style.font_weight = FW_BOLD;
            style.margin = { 10, 0, 4, 0 };
        } else if (tag == L"p") {
            style.display = Display::Block;
            style.margin = { 6, 0, 10, 0 };
            style.line_height = 22;
        } else if (tag == L"a") {
            style.display = Display::Inline;
            style.color = Color(0, 210, 180);
            style.is_underline = true;
        } else if (tag == L"b" || tag == L"strong") {
            style.display = Display::Inline;
            style.font_weight = FW_BOLD;
        } else if (tag == L"i" || tag == L"em") {
            style.display = Display::Inline;
            style.is_italic = true;
        } else if (tag == L"span" || tag == L"small" || tag == L"label" || tag == L"cite") {
            style.display = Display::Inline;
        } else if (tag == L"ul" || tag == L"ol") {
            style.display = Display::Block;
            style.margin = { 8, 0, 12, 24 };
        } else if (tag == L"li") {
            style.display = Display::Block;
            style.margin = { 3, 0, 3, 0 };
        } else if (tag == L"table") {
            style.display = Display::Block;
            style.margin = { 8, 0, 8, 0 };
        } else if (tag == L"tr") {
            style.display = Display::Block;
        } else if (tag == L"td" || tag == L"th") {
            style.display = Display::Block;
        } else if (tag == L"pre" || tag == L"code") {
            style.display = (tag == L"pre") ? Display::Block : Display::Inline;
            style.font_family = L"Consolas";
            style.background_color = Color(22, 27, 34);
            style.color = Color(126, 231, 135);
            style.border_width = 1;
            style.border_color = Color(48, 54, 61);
            style.border_radius = 6;
            style.padding = { 6, 10, 6, 10 };
            if (tag == L"pre") style.margin = { 10, 0, 14, 0 };
        } else if (tag == L"blockquote") {
            style.display = Display::Block;
            style.background_color = Color(22, 27, 34);
            style.border_width = 1;
            style.border_color = Color(0, 210, 180);
            style.border_radius = 4;
            style.padding = { 10, 16, 10, 16 };
            style.margin = { 10, 0, 10, 0 };
            style.is_italic = true;
        } else if (tag == L"hr") {
            style.display = Display::Block;
            style.border_width = 1;
            style.border_color = Color(48, 54, 61);
            style.margin = { 14, 0, 14, 0 };
            style.height = 1;
        } else if (tag == L"button") {
            style.display = Display::InlineBlock;
            style.background_color = Color(0, 210, 180);
            style.color = Color(4, 18, 16);
            style.font_weight = FW_BOLD;
            style.border_radius = 6;
            style.padding = { 6, 14, 6, 14 };
            style.margin = { 4, 4, 4, 4 };
        } else if (tag == L"input") {
            style.display = Display::InlineBlock;
            style.background_color = Color(22, 27, 34);
            style.color = Color(240, 246, 252);
            style.border_width = 1;
            style.border_color = Color(48, 54, 61);
            style.border_radius = 6;
            style.padding = { 6, 10, 6, 10 };
            style.margin = { 4, 4, 4, 4 };
        } else if (tag == L"img") {
            style.display = Display::InlineBlock;
        } else if (tag == L"head" || tag == L"style" || tag == L"script" || tag == L"meta" || tag == L"link" || tag == L"title" || tag == L"svg" || tag == L"path" || tag == L"template" || tag == L"noscript") {
            style.display = Display::None;
        } else {
            style.display = Display::Block;
        }
    }

    static void apply_declarations(
        const std::vector<CSSDeclaration>& decls,
        ComputedStyle& style,
        bool font_size_only = false
    ) {
        for (const auto& decl : decls) {
            const std::wstring& p = decl.property;
            const std::wstring& v = decl.value;
            if ((p == L"font-size") != font_size_only) continue;

            if (p == L"color") {
                style.color = Color::parse(v);
            } else if (p == L"background-color" || p == L"background") {
                style.background_color = Color::parse(v);
            } else if (p == L"font-size") {
                int px = parse_pixels(v, style.font_size, style.font_size);
                if (px > 0) style.font_size = px;
            } else if (p == L"font-weight") {
                if (v == L"bold" || v == L"600" || v == L"700" || v == L"800" || v == L"900") style.font_weight = FW_BOLD;
                else if (v == L"500") style.font_weight = FW_SEMIBOLD;
                else if (v == L"normal" || v == L"400") style.font_weight = FW_NORMAL;
            } else if (p == L"font-style") {
                style.is_italic = (v == L"italic");
            } else if (p == L"font-family") {
                std::wstring family = trim(v);
                size_t comma = family.find(L',');
                if (comma != std::wstring::npos) family = trim(family.substr(0, comma));
                if (family.size() >= 2 &&
                    ((family.front() == L'\'' && family.back() == L'\'') ||
                     (family.front() == L'"' && family.back() == L'"'))) {
                    family = family.substr(1, family.size() - 2);
                }
                if (!family.empty()) style.font_family = family;
            } else if (p == L"text-align") {
                if (v == L"center") style.text_align = TextAlign::Center;
                else if (v == L"right") style.text_align = TextAlign::Right;
                else style.text_align = TextAlign::Left;
            } else if (p == L"text-decoration") {
                style.is_underline = (v.find(L"underline") != std::wstring::npos);
            } else if (p == L"display") {
                if (v == L"none") style.display = Display::None;
                else if (v == L"inline") style.display = Display::Inline;
                else if (v == L"inline-block") style.display = Display::InlineBlock;
                else style.display = Display::Block;
            } else if (p == L"margin") {
                parse_box_insets(v, style.margin, style.font_size);
            } else if (p == L"margin-top") {
                style.margin.top = parse_pixels(v, style.margin.top, style.font_size);
            } else if (p == L"margin-bottom") {
                style.margin.bottom = parse_pixels(v, style.margin.bottom, style.font_size);
            } else if (p == L"margin-left") {
                style.margin.left = parse_pixels(v, style.margin.left, style.font_size);
            } else if (p == L"margin-right") {
                style.margin.right = parse_pixels(v, style.margin.right, style.font_size);
            } else if (p == L"padding") {
                parse_box_insets(v, style.padding, style.font_size);
            } else if (p == L"padding-top") {
                style.padding.top = parse_pixels(v, style.padding.top, style.font_size);
            } else if (p == L"padding-bottom") {
                style.padding.bottom = parse_pixels(v, style.padding.bottom, style.font_size);
            } else if (p == L"padding-left") {
                style.padding.left = parse_pixels(v, style.padding.left, style.font_size);
            } else if (p == L"padding-right") {
                style.padding.right = parse_pixels(v, style.padding.right, style.font_size);
            } else if (p == L"border" || p == L"border-top" || p == L"border-bottom") {
                parse_border(v, style.border_width, style.border_color);
            } else if (p == L"border-width") {
                style.border_width = parse_pixels(v, style.border_width, style.font_size);
            } else if (p == L"border-color") {
                style.border_color = Color::parse(v);
            } else if (p == L"border-radius") {
                style.border_radius = parse_pixels(v, style.border_radius, style.font_size);
            } else if (p == L"width") {
                style.width_percent = parse_percent(v);
                style.width = style.width_percent >= 0.0f ? -1 : parse_pixels(v, -1, style.font_size);
            } else if (p == L"max-width") {
                style.max_width_percent = parse_percent(v);
                if (style.max_width_percent < 0.0f)
                    style.max_width = parse_pixels(v, style.max_width, style.font_size);
            } else if (p == L"height") {
                style.height = parse_pixels(v, -1, style.font_size);
            } else if (p == L"line-height") {
                double multiplier = 0.0;
                size_t consumed = 0;
                std::wstring line_height_value = trim(v);
                try {
                    multiplier = std::stod(line_height_value, &consumed);
                } catch (...) {
                    consumed = 0;
                }
                if (consumed == line_height_value.size() && multiplier > 0.0 && multiplier < 4.0) {
                    style.line_height = static_cast<int>(std::lround(multiplier * style.font_size));
                } else {
                    style.line_height = parse_pixels(v, style.line_height, style.font_size);
                }
            } else if (p == L"opacity") {
                try {
                    float f = std::stof(v);
                    style.opacity = std::clamp(f, 0.0f, 1.0f);
                } catch (...) {}
            } else if (p == L"z-index") {
                try {
                    style.z_index = std::stoi(v);
                } catch (...) {}
            }
        }
    }

    static int parse_pixels(const std::wstring& val, int fallback, int font_size = 16) {
        std::wstring v = trim(val);
        if (v.empty() || v == L"auto") return fallback;
        size_t consumed = 0;
        double value = 0.0;
        try {
            value = std::stod(v, &consumed);
        } catch (...) {
            return fallback;
        }
        if (consumed == 0) return fallback;
        std::wstring unit = to_lower(trim(v.substr(consumed)));
        if (unit == L"em" || unit == L"rem") value *= font_size;
        else if (unit == L"pt") value *= 96.0 / 72.0;
        else if (!unit.empty() && unit != L"px") return fallback;
        return static_cast<int>(std::lround(value));
    }

    static float parse_percent(const std::wstring& val) {
        std::wstring v = trim(val);
        if (v.empty() || v.back() != L'%') return -1.0f;
        try {
            float value = std::stof(v.substr(0, v.size() - 1));
            return value >= 0.0f ? (std::min)(value, 100.0f) : -1.0f;
        } catch (...) {
            return -1.0f;
        }
    }

    static void parse_box_insets(const std::wstring& val, EdgeInsets& insets, int font_size) {
        std::wstringstream ss(val);
        std::vector<int> nums;
        std::wstring item;
        while (ss >> item) {
            nums.push_back(parse_pixels(item, 0, font_size));
        }
        if (nums.size() == 1) {
            insets = { nums[0], nums[0], nums[0], nums[0] };
        } else if (nums.size() == 2) {
            insets = { nums[0], nums[1], nums[0], nums[1] };
        } else if (nums.size() == 3) {
            insets = { nums[0], nums[1], nums[2], nums[1] };
        } else if (nums.size() == 4) {
            insets = { nums[0], nums[1], nums[2], nums[3] };
        }
    }

    static void parse_border(const std::wstring& val, int& width, Color& color) {
        std::wstringstream ss(val);
        std::wstring part;
        while (ss >> part) {
            if (iswdigit(part[0])) {
                try { width = std::stoi(part); } catch (...) {}
            } else if (part == L"solid" || part == L"none" || part == L"dashed") {
                if (part == L"none") width = 0;
            } else {
                color = Color::parse(part);
            }
        }
    }
};

} // namespace engine
