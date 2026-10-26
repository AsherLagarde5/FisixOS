#pragma once
#include "engine_dom.h"
#include "engine_css.h"
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <algorithm>

namespace engine {

struct InlineItem {
    std::wstring text;
    Rect bounds;
    ComputedStyle style;
    std::wstring href;
    bool is_link = false;
    bool is_image = false;
    std::wstring image_src;
    bool is_button = false;
    bool is_input = false;
    std::wstring input_value;
    std::wstring input_placeholder;
};

struct LineBox {
    int y = 0;
    int height = 0;
    int width = 0;
    std::vector<InlineItem> items;
};

class LayoutBox {
public:
    DOMNode* node = nullptr;
    ComputedStyle style;
    Rect bounds; // Box dimensions in document coordinates
    Display display = Display::Block;

    std::vector<std::unique_ptr<LayoutBox>> children;
    std::vector<LineBox> lines;

    std::wstring href;
    bool is_link = false;
    bool is_image = false;
    std::wstring image_src;
    bool is_button = false;
    bool is_input = false;
    std::wstring input_value;
    std::wstring input_placeholder;

    LayoutBox() = default;

    std::wstring tag_name() const {
        if (node && node->type == NodeType::Element) {
            return static_cast<DOMElement*>(node)->tag_name;
        }
        return L"";
    }

    const LayoutBox* hit_test(int doc_x, int doc_y) const {
        if (!bounds.contains(doc_x, doc_y)) return nullptr;

        for (const auto& line : lines) {
            for (const auto& item : line.items) {
                if (item.bounds.contains(doc_x, doc_y)) {
                    return this;
                }
            }
        }

        for (const auto& child : children) {
            const LayoutBox* hit = child->hit_test(doc_x, doc_y);
            if (hit) return hit;
        }

        return this;
    }

    std::wstring get_href_at(int doc_x, int doc_y) const {
        for (const auto& line : lines) {
            for (const auto& item : line.items) {
                if (item.is_link && item.bounds.contains(doc_x, doc_y)) {
                    return item.href;
                }
            }
        }
        if (is_link && bounds.contains(doc_x, doc_y)) {
            return href;
        }
        for (const auto& child : children) {
            std::wstring ch_href = child->get_href_at(doc_x, doc_y);
            if (!ch_href.empty()) return ch_href;
        }
        return L"";
    }
};

class LayoutEngine {
public:
    static std::unique_ptr<LayoutBox> layout_document(
        const std::shared_ptr<DOMDocument>& doc,
        int viewport_width,
        HDC hdc
    ) {
        if (!doc) return nullptr;

        std::vector<StyleSheet> stylesheets;
        for (const auto& css_text : doc->embedded_stylesheets) {
            stylesheets.push_back(StyleSheet::parse(css_text));
        }

        auto root = build_box(doc.get(), stylesheets);
        if (!root) return nullptr;

        int doc_width = (std::max)(viewport_width, 240);
        int current_y = 0;
        layout_box(root.get(), 0, current_y, doc_width, hdc);

        return root;
    }

private:
    static std::unique_ptr<LayoutBox> build_box(
        DOMNode* node,
        const std::vector<StyleSheet>& stylesheets,
        const ComputedStyle* parent_style = nullptr
    ) {
        if (!node) return nullptr;

        auto box = std::make_unique<LayoutBox>();
        box->node = node;
        box->style = StyleResolver::resolve(node, stylesheets, parent_style);
        box->display = box->style.display;

        if (box->display == Display::None) return nullptr;

        if (node->type == NodeType::Element) {
            auto* elem = static_cast<DOMElement*>(node);
            const std::wstring& tag = elem->tag_name;

            if (tag == L"a") {
                box->is_link = true;
                box->href = elem->get_attribute(L"href");
            } else if (tag == L"img") {
                box->is_image = true;
                box->image_src = elem->get_attribute(L"src");
            } else if (tag == L"button") {
                box->is_button = true;
            } else if (tag == L"input") {
                std::wstring type = to_lower(elem->get_attribute(L"type"));
                if (type == L"submit" || type == L"button" || type == L"reset") {
                    box->is_button = true;
                    box->input_value = elem->get_attribute(L"value");
                } else if (type == L"hidden") {
                    return nullptr;
                } else {
                    box->is_input = true;
                    box->input_value = elem->get_attribute(L"value");
                    box->input_placeholder = elem->get_attribute(L"placeholder");
                }
            }
        }

        for (const auto& child : node->children) {
            auto child_box = build_box(child.get(), stylesheets, &box->style);
            if (child_box) {
                box->children.push_back(std::move(child_box));
            }
        }

        // Promote box to Block if it contains any Block children
        for (const auto& child : box->children) {
            if (child->display == Display::Block) {
                box->display = Display::Block;
                break;
            }
        }

        return box;
    }

    static void layout_box(LayoutBox* box, int x, int& y, int available_width, HDC hdc) {
        if (!box || box->display == Display::None) return;

        y += box->style.margin.top;

        int box_x = x + box->style.margin.left;
        int available_box_width = (std::max)(0, available_width - box->style.margin.left - box->style.margin.right);
        int box_w = (box->style.width > 0)
            ? box->style.width
            : (box->style.width_percent >= 0.0f
                ? static_cast<int>(available_box_width * box->style.width_percent / 100.0f)
                : available_box_width);
        if (box->style.max_width > 0) box_w = (std::min)(box_w, box->style.max_width);
        if (box->style.max_width_percent >= 0.0f) {
            box_w = (std::min)(box_w, static_cast<int>(available_box_width * box->style.max_width_percent / 100.0f));
        }
        int content_x = box_x + box->style.padding.left + box->style.border_width;
        int content_w = (std::max)(10, box_w - box->style.padding.left - box->style.padding.right - (2 * box->style.border_width));
        int content_y = y + box->style.padding.top + box->style.border_width;

        box->bounds.x = box_x;
        box->bounds.y = y;
        box->bounds.width = box_w;
        box->lines.clear();

        // 1. Table Row (<tr>) Horizontal Cell Distribution
        if (box->tag_name() == L"tr" && !box->children.empty()) {
            size_t num_cells = box->children.size();
            int cell_w = (std::max)(20, content_w / (int)num_cells);
            int cur_cell_x = content_x;
            int max_cell_h = 0;

            for (auto& cell : box->children) {
                int cell_y = content_y;
                layout_box(cell.get(), cur_cell_x, cell_y, cell_w, hdc);
                max_cell_h = (std::max)(max_cell_h, cell->bounds.height);
                cur_cell_x += cell_w;
            }

            box->bounds.height = max_cell_h + box->style.padding.top + box->style.padding.bottom + (2 * box->style.border_width);
            y = box->bounds.y + box->bounds.height + box->style.margin.bottom;
            return;
        }

        // 2. Standard Block vs Inline Runs
        int current_cursor_y = content_y;
        size_t child_idx = 0;
        size_t total_children = box->children.size();

        while (child_idx < total_children) {
            if (box->children[child_idx]->display == Display::Block) {
                // Layout block child independently
                layout_box(box->children[child_idx].get(), content_x, current_cursor_y, content_w, hdc);
                ++child_idx;
            } else {
                // Collect contiguous inline elements run ONLY (do NOT crawl into child blocks)
                std::vector<InlineItem> run_items;
                while (child_idx < total_children && box->children[child_idx]->display != Display::Block) {
                    collect_inline_items_direct(box->children[child_idx].get(), run_items, box->children[child_idx]->style, box->children[child_idx]->href, box->children[child_idx]->is_link);
                    ++child_idx;
                }

                if (!run_items.empty()) {
                    std::vector<LineBox> run_lines;
                    int run_height = layout_inline_items(run_items, run_lines, content_x, current_cursor_y, content_w, box->style.text_align, hdc);
                    for (auto& l : run_lines) {
                        box->lines.push_back(std::move(l));
                    }
                    current_cursor_y += run_height + 4;
                }
            }
        }

        // If this box is a leaf text node directly
        if (box->node && box->node->type == NodeType::Text && box->children.empty()) {
            std::vector<InlineItem> items;
            collect_inline_items_direct(box, items, box->style, box->href, box->is_link);
            int text_h = layout_inline_items(items, box->lines, content_x, current_cursor_y, content_w, box->style.text_align, hdc);
            current_cursor_y += text_h;
        }

        if (box->is_image) {
            int img_h = (box->style.height > 0) ? box->style.height : 100;
            current_cursor_y = (std::max)(current_cursor_y, content_y + img_h);
        }

        int total_content_h = (std::max)(current_cursor_y - content_y, (box->style.height > 0 ? box->style.height : 0));
        box->bounds.height = total_content_h + box->style.padding.top + box->style.padding.bottom + (2 * box->style.border_width);

        y = box->bounds.y + box->bounds.height + box->style.margin.bottom;
    }

    static void collect_inline_items_direct(
        LayoutBox* box,
        std::vector<InlineItem>& out_items,
        const ComputedStyle& inherit_style,
        const std::wstring& current_href,
        bool is_link
    ) {
        if (!box || box->display == Display::None) return;

        // STOP if box is a Block element
        if (box->display == Display::Block && box->node && box->node->type != NodeType::Text) {
            return;
        }

        // Handle atomic Inline-Block elements (Links, Badges, Buttons, Styled Pills)
        if (box->is_link || box->style.display == Display::InlineBlock || !box->style.background_color.is_transparent()) {
            InlineItem item;
            item.style = box->style;
            item.href = box->is_link ? box->href : current_href;
            item.is_link = box->is_link || is_link;
            item.text = box->node ? box->node->text_content() : L"";
            if (item.text.empty() && box->node && box->node->type == NodeType::Text) {
                item.text = static_cast<DOMText*>(box->node)->text;
            }
            if (!item.text.empty()) {
                out_items.push_back(item);
                return;
            }
        }

        if (box->node && box->node->type == NodeType::Text) {
            auto* text_node = static_cast<DOMText*>(box->node);
            std::wstringstream ss(text_node->text);
            std::wstring word;
            while (ss >> word) {
                while (word.size() > 50) {
                    InlineItem item;
                    item.text = word.substr(0, 50);
                    item.style = inherit_style;
                    item.href = current_href;
                    item.is_link = is_link;
                    out_items.push_back(item);
                    word = word.substr(50);
                }
                if (!word.empty()) {
                    InlineItem item;
                    item.text = word;
                    item.style = inherit_style;
                    item.href = current_href;
                    item.is_link = is_link;
                    out_items.push_back(item);
                }
            }
            return;
        }

        if (box->is_image) {
            InlineItem item;
            item.is_image = true;
            item.image_src = box->image_src;
            item.style = box->style;
            item.href = current_href;
            item.is_link = is_link;
            out_items.push_back(item);
            return;
        }

        if (box->is_button || box->is_input) {
            InlineItem item;
            item.is_button = box->is_button;
            item.is_input = box->is_input;
            item.input_value = box->input_value;
            item.input_placeholder = box->input_placeholder;
            item.style = box->style;
            item.href = current_href;
            item.is_link = is_link;
            if (box->is_button) {
                item.text = !box->input_value.empty() ? box->input_value : (box->node ? box->node->text_content() : L"");
                if (item.text.empty()) item.text = L"Button";
            }
            out_items.push_back(item);
            return;
        }

        std::wstring href = box->is_link ? box->href : current_href;
        bool link_state = box->is_link || is_link;

        for (const auto& child : box->children) {
            // Only recurse into inline children
            if (child->display != Display::Block) {
                collect_inline_items_direct(child.get(), out_items, child->style, href, link_state);
            }
        }
    }

    static int layout_inline_items(
        const std::vector<InlineItem>& items,
        std::vector<LineBox>& out_lines,
        int start_x,
        int start_y,
        int max_width,
        TextAlign align,
        HDC hdc
    ) {
        out_lines.clear();
        if (items.empty()) return 0;

        LineBox current_line;
        current_line.y = start_y;
        int cur_x = start_x;
        int line_height = 24;

        HFONT default_font = CreateFontW(
            16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        HGDIOBJ old_font = SelectObject(hdc, default_font);

        for (size_t i = 0; i < items.size(); ++i) {
            InlineItem item = items[i];

            int item_w = 0;
            int item_h = 0;

            if (item.is_image) {
                item_w = (item.style.width > 0)
                    ? item.style.width
                    : (item.style.width_percent >= 0.0f
                        ? static_cast<int>(max_width * item.style.width_percent / 100.0f)
                        : 120);
                if (item.style.max_width > 0) item_w = (std::min)(item_w, item.style.max_width);
                if (item.style.max_width_percent >= 0.0f) {
                    item_w = (std::min)(item_w, static_cast<int>(max_width * item.style.max_width_percent / 100.0f));
                }
                item_w = (std::max)(1, item_w);
                item_h = (item.style.height > 0) ? item.style.height : 60;
                line_height = (std::max)(line_height, item_h);
            } else if (item.is_button || item.is_input) {
                std::wstring label = !item.text.empty() ? item.text : (!item.input_value.empty() ? item.input_value : item.input_placeholder);
                if (label.empty()) label = L"Search";
                SIZE sz = {};
                GetTextExtentPoint32W(hdc, label.c_str(), (int)label.length(), &sz);
                item_w = (int)sz.cx + 28;
                item_h = 36;
                line_height = (std::max)(line_height, item_h);
            } else {
                HFONT word_font = CreateFontW(
                    item.style.font_size > 0 ? item.style.font_size : 16, 0, 0, 0, item.style.font_weight,
                    item.style.is_italic ? TRUE : FALSE,
                    item.style.is_underline ? TRUE : FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                    item.style.font_family.empty() ? L"Segoe UI" : item.style.font_family.c_str()
                );
                SelectObject(hdc, word_font);

                SIZE text_size = {};
                GetTextExtentPoint32W(hdc, item.text.c_str(), (int)item.text.length(), &text_size);
                int pad_h = item.style.padding.left + item.style.padding.right;
                int pad_v = item.style.padding.top + item.style.padding.bottom;
                item_w = (int)text_size.cx + pad_h;
                item_h = (int)text_size.cy + pad_v;
                line_height = (std::max)(line_height, (std::max)(item_h + 6, item.style.line_height));

                DeleteObject(word_font);
            }

            // Word-wrap check
            if (cur_x + item_w > start_x + max_width && !current_line.items.empty()) {
                current_line.height = line_height;
                current_line.width = cur_x - start_x;
                out_lines.push_back(current_line);

                current_line.items.clear();
                current_line.y += line_height + 8;
                cur_x = start_x;
            }

            item.bounds = Rect{ cur_x, current_line.y, item_w, item_h };
            current_line.items.push_back(item);

            SIZE space_size = {};
            GetTextExtentPoint32W(hdc, L" ", 1, &space_size);
            int margin_r = item.style.margin.right > 0 ? item.style.margin.right : 0;
            int sp = (item.is_button || item.is_input || item.is_link || !item.style.background_color.is_transparent()) ? (int)space_size.cx + 8 + margin_r : (int)space_size.cx;
            cur_x += item_w + sp;
        }

        if (!current_line.items.empty()) {
            current_line.height = line_height;
            current_line.width = cur_x - start_x;
            out_lines.push_back(current_line);
        }

        SelectObject(hdc, old_font);
        DeleteObject(default_font);

        // Apply text alignment
        if (align == TextAlign::Center || align == TextAlign::Right) {
            for (auto& line : out_lines) {
                int shift = (align == TextAlign::Center)
                    ? (max_width - line.width) / 2
                    : (max_width - line.width);
                if (shift > 0) {
                    for (auto& it : line.items) {
                        it.bounds.x += shift;
                    }
                }
            }
        }

        int total_height = 0;
        if (!out_lines.empty()) {
            total_height = (out_lines.back().y + out_lines.back().height) - start_y;
        }
        return total_height;
    }
};

} // namespace engine
