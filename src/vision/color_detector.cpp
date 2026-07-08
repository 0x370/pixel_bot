// src/vision/color_detector.cpp — HSV color-thresholding detector.
//
// BGRA→HSV on the FOV crop → in_range_hsv over config color_ranges → mask →
// connected_components(min_area) → select_target (centroid→head→nearest→
// hysteresis). Works on the crop; coords translated back to full-frame.
#include "color_detector.hpp"
#include "detector_util.hpp"
#include "primitives.hpp"
#include "config.hpp"
#include "error.hpp"
#include <vector>

namespace pixelbot {

Target ColorDetector::detect(const Frame& frame, const DetectionConfig& cfg) {
    Crop crop = make_crop(frame, cfg);
    const int w = crop.frame.width, h = crop.frame.height;
    if (w <= 0 || h <= 0) { last_ = Target{}; return Target{}; }

    const std::size_t n = static_cast<std::size_t>(w) * h;
    std::vector<std::byte> hsv(n * 3);
    std::vector<std::uint8_t> mask(n);

    bgra_to_hsv(crop.frame.data, w, h, crop.frame.stride, hsv);
    in_range_hsv(hsv, w, h, w * 3, cfg.color_ranges, mask);
    auto comps = connected_components(mask, w, h, w, cfg.min_area);
    return select_target(comps, crop.x0, crop.y0, crop.cx, crop.cy, cfg, last_);
}

} // namespace pixelbot
