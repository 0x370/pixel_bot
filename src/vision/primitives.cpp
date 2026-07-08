// src/vision/primitives.cpp — hand-written CV (no OpenCV). Correctness-critical.
#include "primitives.hpp"
#include "error.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace pixelbot {

namespace {
inline std::uint8_t bgra_b(const std::byte* p) noexcept {
    return static_cast<std::uint8_t>(p[0]);
}
inline std::uint8_t bgra_g(const std::byte* p) noexcept {
    return static_cast<std::uint8_t>(p[1]);
}
inline std::uint8_t bgra_r(const std::byte* p) noexcept {
    return static_cast<std::uint8_t>(p[2]);
}
} // namespace

std::uint8_t gray_at(std::span<const std::byte> bgra, int x, int y,
                     int stride) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(bgra.data())
                    + static_cast<std::ptrdiff_t>(y) * stride + x * 4;
    const int r = p[2], g = p[1], b = p[0];
    return static_cast<std::uint8_t>((299 * r + 587 * g + 114 * b) / 1000);
}

void bgra_to_gray(std::span<const std::byte> bgra, int w, int h, int bgra_stride,
                  std::span<std::uint8_t> gray) {
    if (static_cast<long>(gray.size()) < static_cast<long>(w) * h)
        throw FatalError{"vision: bgra_to_gray output buffer too small"};
    for (int y = 0; y < h; ++y) {
        const auto* src = reinterpret_cast<const unsigned char*>(bgra.data())
                          + static_cast<std::ptrdiff_t>(y) * bgra_stride;
        auto* dst = gray.data() + static_cast<std::ptrdiff_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            const int r = src[x * 4 + 2], g = src[x * 4 + 1], b = src[x * 4 + 0];
            dst[x] = static_cast<std::uint8_t>((299 * r + 587 * g + 114 * b) / 1000);
        }
    }
}

// BGR (not RGB — X11 ZPixmap is B,G,R,X) → HSV, OpenCV 8-bit convention:
// H in [0,180), S/V in [0,255]. H is halved from the usual 0-360 range so the
// config's integer H ranges (0-180) match the color-aimbot reference.
void bgra_to_hsv(std::span<const std::byte> bgra, int w, int h, int bgra_stride,
                 std::span<std::byte> hsv) {
    const int hsv_stride = w * 3;
    if (static_cast<long>(hsv.size()) < static_cast<long>(hsv_stride) * h)
        throw FatalError{"vision: bgra_to_hsv output buffer too small"};
    for (int y = 0; y < h; ++y) {
        const auto* src = reinterpret_cast<const unsigned char*>(bgra.data())
                          + static_cast<std::ptrdiff_t>(y) * bgra_stride;
        auto* dst = reinterpret_cast<unsigned char*>(hsv.data())
                    + static_cast<std::ptrdiff_t>(y) * hsv_stride;
        for (int x = 0; x < w; ++x) {
            const int b = src[x * 4 + 0], g = src[x * 4 + 1], r = src[x * 4 + 2];
            const int v = std::max({r, g, b});
            const int mn = std::min({r, g, b});
            const int diff = v - mn;
            int s = v == 0 ? 0 : (diff * 255) / v;
            int hh;
            if (diff == 0) {
                hh = 0;
            } else if (v == r) {
                hh = (60 * (g - b)) / diff;
            } else if (v == g) {
                hh = 120 + (60 * (b - r)) / diff;
            } else { // v == b
                hh = 240 + (60 * (r - g)) / diff;
            }
            if (hh < 0) hh += 360;
            hh /= 2;  // OpenCV 8-bit: [0,180)
            dst[x * 3 + 0] = static_cast<unsigned char>(hh);
            dst[x * 3 + 1] = static_cast<unsigned char>(s);
            dst[x * 3 + 2] = static_cast<unsigned char>(v);
        }
    }
}

void in_range_hsv(std::span<const std::byte> hsv, int w, int h, int hsv_stride,
                  const std::vector<ColorRange>& ranges,
                  std::span<std::uint8_t> mask) {
    if (static_cast<long>(mask.size()) < static_cast<long>(w) * h)
        throw FatalError{"vision: in_range_hsv output buffer too small"};
    if (hsv_stride == 0) hsv_stride = w * 3;
    for (int y = 0; y < h; ++y) {
        const auto* src = reinterpret_cast<const unsigned char*>(hsv.data())
                          + static_cast<std::ptrdiff_t>(y) * hsv_stride;
        auto* dst = mask.data() + static_cast<std::ptrdiff_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            const int hh = src[x * 3 + 0];
            const int ss = src[x * 3 + 1];
            const int vv = src[x * 3 + 2];
            bool in = false;
            for (const auto& r : ranges) {
                if (hh >= r.h0 && hh <= r.h1 &&
                    ss >= r.s0 && ss <= r.s1 &&
                    vv >= r.v0 && vv <= r.v1) { in = true; break; }
            }
            dst[x] = in ? 255 : 0;
        }
    }
}

// 3x3 Sobel. gx = [-1 0 1; -2 0 2; -1 0 1], gy = transpose. Accumulators are
// reset to 0 per pixel (the canonical Sobel bug is forgetting this). Borders
// set to 0 (no padding extension).
void sobel(std::span<const std::uint8_t> gray, int w, int h, int stride,
           std::span<std::uint8_t> mag) {
    if (static_cast<long>(mag.size()) < static_cast<long>(w) * h)
        throw FatalError{"vision: sobel output buffer too small"};
    const auto* g = gray.data();
    auto* m = mag.data();
    for (int y = 0; y < h; ++y) {
        auto* out = m + static_cast<std::ptrdiff_t>(y) * w;
        if (y == 0 || y == h - 1) {
            std::fill_n(out, w, std::uint8_t{0});
            continue;
        }
        for (int x = 0; x < w; ++x) {
            if (x == 0 || x == w - 1) { out[x] = 0; continue; }
            // gx
            int gx = 0;
            gx += -1 * g[(y - 1) * stride + (x - 1)];
            gx +=  1 * g[(y - 1) * stride + (x + 1)];
            gx += -2 * g[(y)     * stride + (x - 1)];
            gx +=  2 * g[(y)     * stride + (x + 1)];
            gx += -1 * g[(y + 1) * stride + (x - 1)];
            gx +=  1 * g[(y + 1) * stride + (x + 1)];
            // gy
            int gy = 0;
            gy += -1 * g[(y - 1) * stride + (x - 1)];
            gy += -2 * g[(y - 1) * stride + (x)];
            gy += -1 * g[(y - 1) * stride + (x + 1)];
            gy +=  1 * g[(y + 1) * stride + (x - 1)];
            gy +=  2 * g[(y + 1) * stride + (x)];
            gy +=  1 * g[(y + 1) * stride + (x + 1)];
            const int mag_v = static_cast<int>(std::sqrt(
                static_cast<double>(gx) * gx + static_cast<double>(gy) * gy));
            out[x] = static_cast<std::uint8_t>(std::min(mag_v, 255));
        }
    }
}

void threshold(std::span<const std::uint8_t> in, std::span<std::uint8_t> out,
               int w, int h, int stride, int t) {
    if (static_cast<long>(out.size()) < static_cast<long>(stride) * h)
        throw FatalError{"vision: threshold output buffer too small"};
    for (int y = 0; y < h; ++y) {
        const auto* src = in.data() + static_cast<std::ptrdiff_t>(y) * stride;
        auto* dst = out.data() + static_cast<std::ptrdiff_t>(y) * stride;
        for (int x = 0; x < w; ++x)
            dst[x] = src[x] >= t ? 255 : 0;
    }
}

// Two-pass connected components with union-find, 8-connectivity. First pass
// assigns provisional labels and unions neighbors; second pass computes the
// bbox and the three moments m00/m10/m01. Components below min_area are dropped.
std::vector<Component> connected_components(std::span<const std::uint8_t> mask,
                                            int w, int h, int stride,
                                            int min_area) {
    const auto* m = mask.data();
    const std::size_t n = static_cast<std::size_t>(w) * h;
    std::vector<int> labels(n, 0);
    std::vector<int> parent{0};  // parent[0] = 0 (background sentinel)

    auto find = [&](int x) -> int {
        // Iterative path-compression find.
        int root = x;
        while (parent[root] != root) root = parent[root];
        while (parent[x] != root) { int nx = parent[x]; parent[x] = root; x = nx; }
        return root;
    };
    auto unite = [&](int a, int b) {
        int ra = find(a), rb = find(b);
        if (ra != rb) parent[std::max(ra, rb)] = std::min(ra, rb);
    };

    int next_label = 1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (m[y * stride + x] == 0) continue;
            // 8-connectivity: look at the 4 already-visited neighbors (W, NW,
            // N, NE) — the other 4 (E, SE, S, SW) are not labeled yet.
            int neighbors[4]{};
            int nc = 0;
            if (x > 0 && labels[y * w + (x - 1)] != 0)
                neighbors[nc++] = labels[y * w + (x - 1)];
            if (y > 0) {
                if (x > 0 && labels[(y - 1) * w + (x - 1)] != 0)
                    neighbors[nc++] = labels[(y - 1) * w + (x - 1)];
                if (labels[(y - 1) * w + x] != 0)
                    neighbors[nc++] = labels[(y - 1) * w + x];
                if (x < w - 1 && labels[(y - 1) * w + (x + 1)] != 0)
                    neighbors[nc++] = labels[(y - 1) * w + (x + 1)];
            }
            if (nc == 0) {
                int lbl = next_label++;
                parent.push_back(lbl);
                labels[y * w + x] = lbl;
            } else {
                int min_lbl = neighbors[0];
                for (int i = 1; i < nc; ++i) min_lbl = std::min(min_lbl, neighbors[i]);
                labels[y * w + x] = min_lbl;
                for (int i = 0; i < nc; ++i) unite(neighbors[i], min_lbl);
            }
        }
    }

    // Second pass: collect stats keyed by root label.
    struct Stats { int x0, y0, x1, y1; long m00, m10, m01; };
    std::unordered_map<int, Stats> stats;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int lbl = labels[y * w + x];
            if (lbl == 0) continue;
            int root = find(lbl);
            auto& s = stats[root];
            if (s.m00 == 0) { s = {x, y, x, y, 0, 0, 0}; }
            s.x0 = std::min(s.x0, x); s.x1 = std::max(s.x1, x);
            s.y0 = std::min(s.y0, y); s.y1 = std::max(s.y1, y);
            s.m00 += 1; s.m10 += x; s.m01 += y;
        }
    }

    std::vector<Component> out;
    out.reserve(stats.size());
    for (auto& [lbl, s] : stats) {
        if (s.m00 < min_area) continue;
        out.push_back(Component{
            .x0 = s.x0, .y0 = s.y0, .x1 = s.x1, .y1 = s.y1,
            .m00 = s.m00, .m10 = s.m10, .m01 = s.m01,
            .area = static_cast<int>(s.m00)});
    }
    std::sort(out.begin(), out.end(),
              [](const Component& a, const Component& b) { return a.area > b.area; });
    return out;
}

std::vector<Component> filter_by_aspect(const std::vector<Component>& comps,
                                        float min_aspect) {
    std::vector<Component> filtered;
    filtered.reserve(comps.size());
    for (const auto& c : comps) {
        const int cw = c.x1 - c.x0 + 1;
        const int ch = c.y1 - c.y0 + 1;
        if (cw <= 0) continue;
        const float aspect = static_cast<float>(ch) / static_cast<float>(cw);
        if (aspect >= min_aspect) filtered.push_back(c);
    }
    return filtered;
}

// Shared target selection. Picks the component whose head (centroid x,
// top + head_offset) is nearest to the screen center, with hysteresis: if a
// previous target exists and the new nearest is much farther than the old one,
// prefer the old target when it's still closer (within hysteresis_factor).
Target select_target(const std::vector<Component>& comps,
                     int crop_x0, int crop_y0,
                     int frame_cx, int frame_cy,
                     const DetectionConfig& cfg, Target& last) {
    if (comps.empty()) {
        // No target this frame: invalidate but DON'T clear last (hysteresis
        // may want to keep aim on a briefly-lost target). The aim controller
        // treats valid=false as "no movement".
        return Target{};
    }
    auto head_of = [&](const Component& c) -> std::pair<int,int> {
        const float cx = static_cast<float>(c.m10) / c.m00;
        const int hx = crop_x0 + static_cast<int>(cx);
        const int hy = crop_y0 + c.y0 + cfg.head_offset;
        return {hx, hy};
    };
    auto dist2 = [&](int hx, int hy) -> float {
        const float dx = hx - frame_cx, dy = hy - frame_cy;
        return dx * dx + dy * dy;
    };

    // Find nearest component by head-to-center distance.
    float best_d2 = std::numeric_limits<float>::max();
    int best_hx = 0, best_hy = 0;
    for (const auto& c : comps) {
        auto [hx, hy] = head_of(c);
        const float d2 = dist2(hx, hy);
        if (d2 < best_d2) { best_d2 = d2; best_hx = hx; best_hy = hy; }
    }

    // Hysteresis: if we had a target, prefer keeping it when the new candidate
    // is far from the old one and the old one is still closer than
    // new_dist * hysteresis_factor. This prevents the aim from jumping to a
    // newly-appearing brighter object across the frame.
    int hx = best_hx, hy = best_hy;
    if (last.valid) {
        const float old_d = std::sqrt(dist2(last.x, last.y));
        const float new_d = std::sqrt(best_d2);
        const float delta = std::sqrt(static_cast<float>(
            (best_hx - last.x) * (best_hx - last.x) +
            (best_hy - last.y) * (best_hy - last.y)));
        if (delta > cfg.hysteresis_distance && old_d < new_d * cfg.hysteresis_factor) {
            // Keep the old target's coordinates; the aim controller uses x,y.
            hx = last.x; hy = last.y;
        }
    }

    const float dist = std::sqrt(dist2(hx, hy));
    Target t;
    t.x = hx;
    t.y = hy;
    t.confidence = std::clamp(1.0f - dist / static_cast<float>(cfg.fov_radius), 0.0f, 1.0f);
    t.valid = dist <= static_cast<float>(cfg.fov_radius);
    last = t;
    return t;
}

} // namespace pixelbot
