#pragma once
// CPU compositor for export parity (closes the "export renders only the top
// layer" gap): stacks RGBA layers bottom-to-top onto an opaque canvas with
// each clip's static transform, using the same geometry as the preview —
// aspect-fit into the canvas, then position/scale/rotation/opacity around
// the center. The D3D12 render graph (docs/06) later replaces the internals
// of both paths without changing this contract.

#include <velocity/engine/model.h>

#include <cstdint>
#include <vector>

namespace velocity::engine {

struct CompositorLayer {
    const std::uint8_t* rgba = nullptr; // straight alpha, 4 bytes/px
    int width = 0;
    int height = 0;
    int strideBytes = 0; // 0 → width * 4
    ClipTransform transform;
};

// Opaque RGBA canvas the layers composite onto (alpha stays 255).
struct CompositeCanvas {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // stride == width * 4

    [[nodiscard]] int stride() const { return width * 4; }
};

// Composites `layers` (bottom-to-top) over black. Layers with invalid
// buffers or non-positive opacity are skipped.
CompositeCanvas compositeLayers(int canvasWidth, int canvasHeight,
                                const std::vector<CompositorLayer>& layers);

// Same, reusing the caller's canvas storage — export calls this per frame
// and must not reallocate a full-frame buffer 30 times a second.
void compositeLayersInto(CompositeCanvas& canvas, int canvasWidth, int canvasHeight,
                         const std::vector<CompositorLayer>& layers);

} // namespace velocity::engine
