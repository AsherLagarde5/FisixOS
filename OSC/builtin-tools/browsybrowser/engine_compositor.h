#pragma once
#include "engine_canvas.h"
#include "engine_paint.h"
#include <vector>
#include <memory>
#include <algorithm>

// -----------------------------------------------------------------------------
// engine_compositor.h
//
// Stage 2 of the Chrome-style render pipeline:
//   Layout Tree → Paint → [COMPOSITE: Layer Tree] → Rasterize
//
// This mirrors Chromium's cc (Chromium Compositor) layer tree.
//
// How it works:
//   1. CompositingLayerBuilder walks the LayoutBox tree and promotes certain
//      boxes into their own CompositingLayer based on Chrome's promotion rules
//      (stacking context, opacity, z-index, transform, fixed-position).
//   2. Each CompositingLayer owns a DisplayList built by DisplayListBuilder.
//   3. Compositor::composite() paints layers back-to-front (painter's algorithm)
//      onto the Canvas, applying layer-level opacity and z-ordering.
//
// Layer promotion triggers (matching Chrome's heuristics):
//   - z-index != 0 on a positioned element  → stacking context → own layer
//   - opacity < 1.0                         → own layer (alpha compositing)
//   - CSS transform (future)                → own layer (GPU compositing)
//   - Root layer                            → always promoted
// -----------------------------------------------------------------------------

namespace engine {
namespace compositor {

using paint::DisplayList;
using paint::DisplayListBuilder;

// ---------------------------------------------------------------------------
// CompositingLayer — a single composited layer.
// Analogous to cc::PictureLayer in Chromium.
// ---------------------------------------------------------------------------

struct CompositingLayer {
    int z_index = 0;
    float opacity = 1.0f;
    Rect bounds;                  // Layer bounds in document space
    DisplayList display_list;     // Paint commands for this layer's content

    // Future: transform matrix, blend mode, will-change hints, GPU texture id
};

// ---------------------------------------------------------------------------
// CompositingLayerBuilder — promotes LayoutBoxes into layers.
// ---------------------------------------------------------------------------

class CompositingLayerBuilder {
public:
    // Build the layer list for a full document, sorted by z-index (back-to-front).
    // scroll_y and viewport are forwarded to the DisplayListBuilder for culling.
    static std::vector<CompositingLayer> build(
        const LayoutBox* root,
        int scroll_y,
        const RECT& viewport,
        const std::wstring& hovered_href
    ) {
        std::vector<CompositingLayer> layers;
        if (!root) return layers;

        // Root layer always exists
        CompositingLayer root_layer;
        root_layer.z_index = 0;
        root_layer.opacity = 1.0f;
        root_layer.bounds  = root->bounds;

        // Recursively collect: boxes that form stacking contexts go to their
        // own layer; everything else accumulates into the root layer.
        collect_layers(root, scroll_y, viewport, hovered_href, root_layer, layers);

        // Always push root layer (even if empty — ensures background is painted)
        layers.insert(layers.begin(), std::move(root_layer));

        // Sort back-to-front by z-index (stable sort preserves DOM order for same z)
        std::stable_sort(layers.begin(), layers.end(),
            [](const CompositingLayer& a, const CompositingLayer& b) {
                return a.z_index < b.z_index;
            }
        );

        return layers;
    }

private:
    static void collect_layers(
        const LayoutBox* box,
        int scroll_y,
        const RECT& viewport,
        const std::wstring& hovered_href,
        CompositingLayer& current_layer,
        std::vector<CompositingLayer>& out_layers
    ) {
        if (!box || box->display == Display::None) return;

        // --- Determine if this box should be promoted to its own layer ---
        const bool has_z_index  = box->style.z_index != 0;
        const bool has_opacity  = box->style.opacity < 1.0f && box->style.opacity >= 0.0f;
        // Future: const bool has_transform = box->style.has_transform;

        const bool promote = has_z_index || has_opacity;

        if (promote) {
            CompositingLayer new_layer;
            new_layer.z_index = box->style.z_index;
            new_layer.opacity = has_opacity ? box->style.opacity : 1.0f;
            new_layer.bounds  = box->bounds;

            // Build the display list for just this subtree
            // We build it by passing just this box as the root
            new_layer.display_list = DisplayListBuilder::build(box, scroll_y, viewport, hovered_href);

            // Collect children into the promoted layer
            for (const auto& child : box->children) {
                if (child->display == Display::Block) {
                    collect_layers(child.get(), scroll_y, viewport, hovered_href, new_layer, out_layers);
                }
            }

            out_layers.push_back(std::move(new_layer));
        } else {
            // Accumulate this box's paint commands into the current layer
            DisplayList box_list = DisplayListBuilder::build(box, scroll_y, viewport, hovered_href);
            for (const auto& cmd : box_list.commands()) {
                current_layer.display_list.push(cmd);
            }

            // Recurse into block children (they might promote themselves)
            for (const auto& child : box->children) {
                if (child->display == Display::Block) {
                    collect_layers(child.get(), scroll_y, viewport, hovered_href, current_layer, out_layers);
                }
            }
        }
    }
};

// ---------------------------------------------------------------------------
// DisplayListRasterizer — replays a DisplayList onto a Canvas.
// Analogous to cc::RasterTask / SkiaPaintCanvas in Chromium.
//
// This is the actual draw call loop — it's the only place that talks
// to GDI+ (via Canvas). No layout or paint logic lives here.
// ---------------------------------------------------------------------------

class DisplayListRasterizer {
public:
    static void rasterize(const DisplayList& list, Canvas& canvas) {
        list.replay([&canvas](const paint::PaintCommand& cmd) {
            std::visit([&canvas](const auto& c) { execute(c, canvas); }, cmd);
        });
    }

private:
    // Overloads for each command type:

    static void execute(const paint::CmdSaveLayer& c, Canvas& canvas) {
        canvas.save_layer(c.opacity);
    }
    static void execute(const paint::CmdRestore&, Canvas& canvas) {
        canvas.restore();
    }
    static void execute(const paint::CmdClipRect& c, Canvas& canvas) {
        canvas.clip_rect(c.clip);
    }
    static void execute(const paint::CmdFillRect& c, Canvas& canvas) {
        canvas.draw_rect(c.bounds, c.color);
    }
    static void execute(const paint::CmdFillRoundedRect& c, Canvas& canvas) {
        canvas.draw_rounded_rect(c.bounds, c.color, c.radius);
    }
    static void execute(const paint::CmdStrokeRoundedRect& c, Canvas& canvas) {
        canvas.stroke_rounded_rect(c.bounds, c.color, c.radius, c.stroke_width);
    }
    static void execute(const paint::CmdStrokeRect& c, Canvas& canvas) {
        canvas.stroke_rect(c.bounds, c.color, c.stroke_width);
    }
    static void execute(const paint::CmdDrawText& c, Canvas& canvas) {
        canvas.draw_text(c.text, c.bounds, c.style, c.color);
    }
    static void execute(const paint::CmdDrawImage& c, Canvas& canvas) {
        canvas.draw_image(c.url, c.bounds);
    }
    static void execute(const paint::CmdDrawButton& c, Canvas& canvas) {
        canvas.draw_button(c.label, c.bounds);
    }
    static void execute(const paint::CmdDrawInput& c, Canvas& canvas) {
        canvas.draw_input(c.value, c.placeholder, c.bounds);
    }
};

// ---------------------------------------------------------------------------
// Compositor — top-level entry point.
// Orchestrates the full pipeline: build layers → sort → rasterize each.
// ---------------------------------------------------------------------------

class Compositor {
public:
    // Full pipeline: Layout root → layer tree → rasterized pixels on canvas.
    //
    // This replaces Renderer::paint_layout_tree() as the single call site.
    static void composite(
        Canvas& canvas,
        const LayoutBox* root,
        int scroll_y,
        const RECT& viewport,
        const std::wstring& hovered_href
    ) {
        // Stage 1: Collect layers (already have the display lists)
        auto layers = CompositingLayerBuilder::build(root, scroll_y, viewport, hovered_href);

        // Stage 2: Rasterize back-to-front (painter's algorithm)
        for (auto& layer : layers) {
            if (layer.display_list.empty()) continue;

            if (layer.opacity < 1.0f) {
                canvas.save_layer(layer.opacity);
            } else {
                canvas.save();
            }

            DisplayListRasterizer::rasterize(layer.display_list, canvas);

            if (layer.opacity < 1.0f) {
                canvas.restore_layer();
            } else {
                canvas.restore();
            }
        }
    }
};

} // namespace compositor
} // namespace engine
