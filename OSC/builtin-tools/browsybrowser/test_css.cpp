#include "engine_css.h"
#include <cassert>

int main() {
    engine::DOMElement paragraph(L"p");
    paragraph.attributes[L"class"] = L"card";
    paragraph.attributes[L"id"] = L"target";

    engine::ComputedStyle parent_style;
    parent_style.color = engine::Color(20, 30, 40);
    parent_style.font_size = 20;

    std::vector<engine::StyleSheet> sheets{
        engine::StyleSheet::parse(
            L"p { color: #111111; }"
            L".card { color: #222222; width: 80%; padding: 1px 2px 3px; margin: 1em 2px; line-height: 1.5; }"
            L"#target { color: #123456; font-size: 1.25em; }"
            L"p { color: #abcdef; }"
        )
    };

    auto style = engine::StyleResolver::resolve(&paragraph, sheets, &parent_style);
    assert(style.color.r == 0x12 && style.color.g == 0x34 && style.color.b == 0x56);
    assert(style.font_size == 25);
    assert(style.width_percent == 80.0f);
    assert(style.padding.top == 1 && style.padding.right == 2);
    assert(style.padding.bottom == 3 && style.padding.left == 2);
    assert(style.margin.top == 25 && style.margin.right == 2);
    assert(style.margin.bottom == 25 && style.margin.left == 2);
    assert(style.line_height == 38);

    paragraph.attributes[L"style"] = L"color: #00ff00";
    style = engine::StyleResolver::resolve(&paragraph, sheets, &parent_style);
    assert(style.color.g == 255 && style.color.r == 0 && style.color.b == 0);

    const auto rgba = engine::Color::parse(L"rgba(1, 2, 3, 0.5)");
    const auto hex_alpha = engine::Color::parse(L"#33669980");
    assert(rgba.r == 1 && rgba.g == 2 && rgba.b == 3 && rgba.a == 127);
    assert(hex_alpha.r == 0x33 && hex_alpha.g == 0x66 && hex_alpha.b == 0x99 && hex_alpha.a == 0x80);
}
