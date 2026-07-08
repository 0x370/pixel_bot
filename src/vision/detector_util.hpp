// src/vision/detector_util.hpp — shared FOV-crop helper for detectors.
//
// All detectors run primitives on a square crop centered on the screen and
// sized by fov_radius, clamped to frame bounds. The crop is a zero-copy view
// (a Frame whose span is offset into the original buffer). Target coordinates
// are translated back to full-frame coords by select_target.
#pragma once
#include "core/frame.hpp"
#include "config.hpp"

namespace pixelbot {

struct Crop {
    Frame frame;      // view over the original frame's bytes (zero-copy)
    int x0, y0;       // full-frame origin of the crop (top-left)
    int cx, cy;       // screen center in full-frame coords
};

// Compute the FOV crop around screen center, clamped to frame bounds.
inline Crop make_crop(const Frame& src, const DetectionConfig& cfg) {
    const int cx = src.width / 2;
    const int cy = src.height / 2;
    const int r = cfg.fov_radius;
    int x0 = cx - r, y0 = cy - r;
    int x1 = cx + r, y1 = cy + r;
    // Clamp to frame bounds.
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > src.width)  x1 = src.width;
    if (y1 > src.height) y1 = src.height;
    const int cw = x1 - x0;
    const int ch = y1 - y0;
    // Offset the data pointer to the crop origin.
    auto* base = reinterpret_cast<std::byte*>(src.data.data())
                 + static_cast<std::ptrdiff_t>(y0) * src.stride
                 + static_cast<std::ptrdiff_t>(x0) * 4;
    return Crop{
        .frame = Frame{
            .data = std::span<std::byte>{base, static_cast<std::size_t>(src.stride) * ch},
            .width = cw,
            .height = ch,
            .stride = src.stride,
            .format = src.format,
        },
        .x0 = x0, .y0 = y0,
        .cx = cx, .cy = cy,
    };
}

} // namespace pixelbot
