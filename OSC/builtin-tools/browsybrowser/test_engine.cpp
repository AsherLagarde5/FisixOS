#include "engine_html.h"
#include "engine_css.h"
#include "engine_layout.h"
#include "engine_network.h"
#include "internal_pages.h"
#include <iostream>

void print_layout(const engine::LayoutBox* box, int depth = 0) {
    if (!box || depth > 8) return;
    std::string indent(depth * 2, ' ');
    std::wcout << indent.c_str() << L"Box [x=" << box->bounds.x << L", y=" << box->bounds.y
               << L", w=" << box->bounds.width << L", h=" << box->bounds.height
               << L", lines=" << box->lines.size() << L"]";
    if (box->node && box->node->type == engine::NodeType::Element) {
        std::wcout << L" <" << static_cast<const engine::DOMElement*>(box->node)->tag_name << L">";
    }
    std::wcout << std::endl;
    for (const auto& line : box->lines) {
        for (const auto& it : line.items) {
            std::wcout << indent.c_str() << L"  LineItem: \"" << it.text << L"\" at (" << it.bounds.x << L", " << it.bounds.y << L", " << it.bounds.width << L", " << it.bounds.height << L")" << std::endl;
        }
    }
    for (const auto& child : box->children) {
        print_layout(child.get(), depth + 1);
    }
}

int main() {
    std::string html = internal_pages::get_page_content(L"browsy://newtab");
    std::wstring whtml(html.begin(), html.end());

    auto doc = engine::HTMLParser::parse(whtml, L"browsy://newtab");
    std::wcout << L"Title: " << doc->title << std::endl;

    HDC hdc = GetDC(nullptr);
    auto layout = engine::LayoutEngine::layout_document(doc, 1200, hdc);
    ReleaseDC(nullptr, hdc);

    std::cout << "\n=== NEW TAB LAYOUT TREE ===" << std::endl;
    print_layout(layout.get(), 0);

    return 0;
}
