#pragma once
#include "engine_layout.h"
#include <variant>
#include <vector>
#include <string>
#include <memory>
#include <functional>

// -----------------------------------------------------------------------------
// engine_paint.h
//
// Stage 1 of the Chrome-style render pipeline:
//   DOM → Style → Layout → [PAINT: Display List] → Compositing → Rasterize
//
// This mirrors Blink's PaintChunk / cc::DisplayItemList.
// The DisplayListBuilder walks the LayoutBox tree and emits typed PaintCommands
// into a DisplayList. No GDI+ / HDC calls happen here — this is pure geometry.
// -----------------------------------------------------------------------------

namespace engine {
namespace paint {

// ---------------------------------------------------------------------------
// Paint Commands — each corresponds to one Skia/GDI+ draw call
// ---------------------------------------------------------------------------

struct CmdSaveLayer {
    float opacity = 1.0f;       // SaveLayer for opacity / blend groups
};

struct CmdRestore {};           // Pop the save stack

struct CmdClipRect {
    Rect clip;
};

struct CmdFillRect {
    Rect bounds;
    Color color;
};

struct CmdFillRoundedRect {
    Rect bounds;
    Color color;
    int radius = 0;
};

struct CmdStrokeRoundedRect {
    Rect bounds;
    Color color;
    int radius = 0;
    int stroke_width = 1;
};

struct CmdStrokeRect {
    Rect bounds;
    Color color;
    int stroke_width = 1;
};

struct CmdDrawText {
    std::wstring text;
    Rect bounds;           // Position + available size
    ComputedStyle style;
    Color color;
    bool is_link = false;
    bool is_hovered = false;
};

struct CmdDrawImage {
    std::wstring url;
    Rect bounds;
};

struct CmdDrawButton {
    std::wstring label;
    Rect bounds;
};

struct CmdDrawInput {
    std::wstring value;
    std::wstring placeholder;
    Rect bounds;
};

// Tagged union of all paint commands
using PaintCommand = std::variant<
    CmdSaveLayer,
    CmdRestore,
    CmdClipRect,
    CmdFillRect,
    CmdFillRoundedRect,
    CmdStrokeRoundedRect,
    CmdStrokeRect,
    CmdDrawText,
    CmdDrawImage,
    CmdDrawButton,
    CmdDrawInput
>;

// ---------------------------------------------------------------------------
// DisplayList — an ordered, replayable sequence of paint commands.
// Analogous to cc::DisplayItemList in Chromium.
// ---------------------------------------------------------------------------

class DisplayList {
public:
    void push(PaintCommand cmd) {
        commands_.push_back(std::move(cmd));
    }

    const std::vector<PaintCommand>& commands() const {
        return commands_;
    }

    bool empty() const { return commands_.empty(); }
    size_t size() const { return commands_.size(); }

    void clear() { commands_.clear(); }

    // Visitor replay — call visitor for each command in order.
    // Used by the rasterizer (Canvas) to execute the list.
    template<typename Visitor>
    void replay(Visitor&& visitor) const {
        for (const auto& cmd : commands_) {
            std::visit(std::forward<Visitor>(visitor), cmd);
        }
    }

private:
    std::vector<PaintCommand> commands_;
};

// ---------------------------------------------------------------------------
// DisplayListBuilder — walks the LayoutBox tree and emits PaintCommands.
// Analogous to Blink's PaintLayerPainter / InlinePainter.
//
// This is the "paint phase" of the pipeline. It is stateless w.r.t. GDI;
// all it knows about is LayoutBox geometry and styles.
// ---------------------------------------------------------------------------

class DisplayListBuilder {
public:
    // Build a complete DisplayList from the layout root.
    // scroll_y and viewport are needed to cull off-screen boxes.
    static DisplayList build(
        const LayoutBox* root,
        int scroll_y,
        const RECT& viewport,
        const std::wstring& hovered_href
    ) {
        DisplayList list;
        if (!root) return list;
        build_box(root, scroll_y, viewport, hovered_href, list);
        return list;
    }

private:
    static void build_box(
        const LayoutBox* box,
        int scroll_y,
        const RECT& viewport,
        const std::wstring& hovered_href,
        DisplayList& list
    ) {
        if (!box || box->display == Display::None) return;

        const int screen_y = box->bounds.y - scroll_y + viewport.top;
        const int screen_x = box->bounds.x + viewport.left;
        const int box_w    = box->bounds.width;
        const int box_h    = box->bounds.height;

        // Viewport culling — don't emit commands for off-screen boxes
        if (screen_y + box_h < viewport.top || screen_y > viewport.bottom) return;

        const Rect screen_bounds{ screen_x, screen_y, box_w, box_h };

        // 1. Background fill
        if (!box->style.background_color.is_transparent()) {
            if (box->style.border_radius > 0) {
                list.push(CmdFillRoundedRect{
                    screen_bounds,
                    box->style.background_color,
                    box->style.border_radius
                });
            } else {
                list.push(CmdFillRect{ screen_bounds, box->style.background_color });
            }
        }

        // 2. Border stroke
        if (box->style.border_width > 0 && !box->style.border_color.is_transparent()) {
            if (box->style.border_radius > 0) {
                list.push(CmdStrokeRoundedRect{
                    screen_bounds,
                    box->style.border_color,
                    box->style.border_radius,
                    box->style.border_width
                });
            } else {
                list.push(CmdStrokeRect{
                    screen_bounds,
                    box->style.border_color,
                    box->style.border_width
                });
            }
        }

        // 3. Image boxes
        if (box->is_image && !box->image_src.empty()) {
            list.push(CmdDrawImage{ box->image_src, screen_bounds });
        }

        // 4. Inline line items
        for (const auto& line : box->lines) {
            for (const auto& item : line.items) {
                const int item_x = item.bounds.x + viewport.left;
                const int item_y = item.bounds.y - scroll_y + viewport.top;

                if (item_y + item.bounds.height < viewport.top || item_y > viewport.bottom)
                    continue;

                const Rect item_screen{ item_x, item_y, item.bounds.width, item.bounds.height };

                if (item.is_image && !item.image_src.empty()) {
                    list.push(CmdDrawImage{ item.image_src, item_screen });
                    continue;
                }

                if (item.is_button) {
                    list.push(CmdDrawButton{ item.text, item_screen });
                    continue;
                }

                if (item.is_input) {
                    list.push(CmdDrawInput{ item.input_value, item.input_placeholder, item_screen });
                    continue;
                }

                // Inline background / border for styled spans
                if (!item.style.background_color.is_transparent()) {
                    const int rad = item.style.border_radius > 0 ? item.style.border_radius : 4;
                    list.push(CmdFillRoundedRect{ item_screen, item.style.background_color, rad });

                    if (item.style.border_width > 0 && !item.style.border_color.is_transparent()) {
                        list.push(CmdStrokeRoundedRect{
                            item_screen, item.style.border_color, rad, item.style.border_width
                        });
                    }
                } else if (item.style.border_width > 0 && !item.style.border_color.is_transparent()) {
                    const int rad = item.style.border_radius > 0 ? item.style.border_radius : 4;
                    list.push(CmdStrokeRoundedRect{
                        item_screen, item.style.border_color, rad, item.style.border_width
                    });
                }

                // Text / link
                Color text_color = item.style.color;
                bool hovered = false;
                if (item.is_link && !hovered_href.empty() && hovered_href == item.href) {
                    text_color = Color(255, 220, 100);
                    hovered = true;
                }

                list.push(CmdDrawText{
                    item.text,
                    item_screen,
                    item.style,
                    text_color,
                    item.is_link,
                    hovered
                });
            }
        }

        // 5. Recurse into block / inline-block children (inline runs are in lines above)
        for (const auto& child : box->children) {
            if (child->display == Display::Block || child->display == Display::InlineBlock) {
                build_box(child.get(), scroll_y, viewport, hovered_href, list);
            }
        }
    }
};

} // namespace paint
} // namespace engine
