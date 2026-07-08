// src/vision/edge_detector.cpp — Sobel edge detector with aspect-ratio filter.
//
// BGRA→gray on the FOV crop → sobel → threshold(edge_threshold) → edge mask →
// connected_components(min_area) → filter by aspect ratio (height >= width *
// edge_min_aspect; player models are taller than wide) → select_target.
#include "edge_detector.hpp"
#include "detector_util.hpp"
#include "primitives.hpp"
#include "config.hpp"
#include <vector>

namespace pixelbot {

Target EdgeDetector::detect(const Frame& frame, const DetectionConfig& cfg) {
    Crop crop = make_crop(frame, cfg);
    const int w = crop.frame.width, h = crop.frame.height;
    if (w <= 0 || h <= 0) { last_ = Target{}; return Target{}; }

    const std::size_t n = static_cast<std::size_t>(w) * h;
    std::vector<std::uint8_t> gray(n), mag(n), edge_mask(n);

    bgra_to_gray(crop.frame.data, w, h, crop.frame.stride, gray);
    sobel(gray, w, h, w, mag);
    threshold(mag, edge_mask, w, h, w, cfg.edge_threshold);
    auto comps = connected_components(edge_mask, w, h, w, cfg.min_area);

    // Filter by aspect ratio: keep only tall components (player models are
    // taller than wide; background edges tend to be wide/horizontal).
    auto filtered = filter_by_aspect(comps, cfg.edge_min_aspect);
    return select_target(filtered, crop.x0, crop.y0, crop.cx, crop.cy, cfg, last_);
}

} // namespace pixelbot
