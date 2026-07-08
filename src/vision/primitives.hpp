// src/vision/primitives.hpp — hand-written vision primitives (no OpenCV).
//
// All operate on spans with explicit stride. No allocations in the hot path
// except the output mask/component buffers (which the caller owns).
#pragma once
#include "config.hpp"
#include "core/detector.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pixelbot {

// --- BGRA → HSV (OpenCV 8-bit convention: H in [0,180), S/V in [0,255]) -----
// Output hsv is 3 bytes/pixel (H,S,V packed), hsv_stride = w*3.
void bgra_to_hsv(std::span<const std::byte> bgra, int w, int h, int bgra_stride,
                 std::span<std::byte> hsv);

// Single-pixel luminance from a BGRA buffer: 0.299R + 0.587G + 0.114B.
[[nodiscard]] std::uint8_t gray_at(std::span<const std::byte> bgra,
                                   int x, int y, int stride) noexcept;

// Whole-frame BGRA → grayscale (1 byte/pixel, gray_stride = w).
void bgra_to_gray(std::span<const std::byte> bgra, int w, int h, int bgra_stride,
                  std::span<std::uint8_t> gray);

// --- Color thresholding: mask[i]=255 if pixel is in ANY of the ranges -------
void in_range_hsv(std::span<const std::byte> hsv, int w, int h, int hsv_stride,
                  const std::vector<ColorRange>& ranges,
                  std::span<std::uint8_t> mask);

// --- Sobel edge magnitude: 3x3 Gx/Gy, mag = clamp(sqrt(gx²+gy²)), borders=0 -
void sobel(std::span<const std::uint8_t> gray, int w, int h, int stride,
           std::span<std::uint8_t> mag);

// --- Binary threshold: out = in >= t ? 255 : 0 -------------------------------
void threshold(std::span<const std::uint8_t> in, std::span<std::uint8_t> out,
               int w, int h, int stride, int t);

// --- Connected components (two-pass union-find, 8-connectivity) -------------
struct Component {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // bounding box (inclusive x1/y1)
    long m00 = 0, m10 = 0, m01 = 0;       // moments; centroid = (m10/m00, m01/m00)
    int area = 0;                         // == m00 (pixel count)
};
// Returns components with area >= min_area, sorted by area descending.
std::vector<Component> connected_components(std::span<const std::uint8_t> mask,
                                            int w, int h, int stride,
                                            int min_area);

// Keep only components whose height/width >= min_aspect (tall-only filter;
// player models are taller than wide; background edges tend to be wide).
std::vector<Component> filter_by_aspect(const std::vector<Component>& comps,
                                        float min_aspect);

// --- Shared target selection (centroid → head → nearest-to-center → hysteresis)
// `comps` are in crop coordinates. `crop_x0/crop_y0` translate crop→full-frame.
// `frame_cx/frame_cy` is the screen center in full-frame coords. `last` is the
// detector's previous target (read for hysteresis, written with the new one).
// Returns a Target with full-frame coordinates, or valid=false if no comps.
Target select_target(const std::vector<Component>& comps,
                     int crop_x0, int crop_y0,
                     int frame_cx, int frame_cy,
                     const DetectionConfig& cfg, Target& last);

} // namespace pixelbot
