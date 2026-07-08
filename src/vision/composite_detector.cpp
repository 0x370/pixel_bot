// src/vision/composite_detector.cpp — color & edge fusion (the default).
//
// Computes both the color mask (HSV thresholding) and the edge mask (Sobel +
// threshold), then ANDs them: a pixel must match the target color AND sit on a
// strong edge. This cuts false positives hard — a red HUD element matching
// the color range rarely also has a model silhouette's vertical edges. Then
// connected_components → select_target (shared with Color/Edge).
#include "composite_detector.hpp"
#include "detector_util.hpp"
#include "primitives.hpp"
#include "config.hpp"
#include <vector>

namespace pixelbot {

Target CompositeDetector::detect(const Frame& frame, const DetectionConfig& cfg) {
    Crop crop = make_crop(frame, cfg);
    const int w = crop.frame.width, h = crop.frame.height;
    if (w <= 0 || h <= 0) { last_ = Target{}; return Target{}; }

    const std::size_t n = static_cast<std::size_t>(w) * h;
    std::vector<std::byte> hsv(n * 3);
    std::vector<std::uint8_t> color_mask(n), gray(n), mag(n), edge_mask(n), fused(n);

    bgra_to_hsv(crop.frame.data, w, h, crop.frame.stride, hsv);
    in_range_hsv(hsv, w, h, w * 3, cfg.color_ranges, color_mask);

    bgra_to_gray(crop.frame.data, w, h, crop.frame.stride, gray);
    sobel(gray, w, h, w, mag);
    threshold(mag, edge_mask, w, h, w, cfg.edge_threshold);

    // Fuse: pixel must be in color range AND on a strong edge.
    for (std::size_t i = 0; i < n; ++i)
        fused[i] = (color_mask[i] != 0 && edge_mask[i] != 0) ? 255 : 0;

    // Filter by aspect ratio: a red HUD bar matches color AND has thin
    // horizontal edges (top/bottom), so color&edge alone isn't enough to
    // reject it. The tall-only aspect filter (shared with EdgeDetector)
    // cuts such wide horizontal bands — the "excellent discernment" goal.
    auto comps = connected_components(fused, w, h, w, cfg.min_area);
    auto filtered = filter_by_aspect(comps, cfg.edge_min_aspect);
    return select_target(filtered, crop.x0, crop.y0, crop.cx, crop.cy, cfg, last_);
}

} // namespace pixelbot
