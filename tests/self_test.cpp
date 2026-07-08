// tests/self_test.cpp — in-process synthetic-frame vision pipeline test.
//
// No X11, no uinput, no game. Synthesizes BGRA frames in-code and asserts:
//  1. ColorDetector finds a 30x60 red rectangle off-center (valid, x/y within 2px).
//  2. EdgeDetector rejects a WIDE rectangle (aspect filter: tall-only).
//  2b. EdgeDetector accepts a tall rectangle.
//  3. CompositeDetector finds the tall red rectangle (color AND edges).
//  3b. CompositeDetector rejects a red HUD bar (color matches but wide, no
//      vertical silhouette — the aspect filter cuts it).
// Proves the vision pipeline end-to-end with no image-library dependency.
//
// Uses CHECK (not assert) so failures surface under NDEBUG/Release builds too.
#include "config.hpp"
#include "core/frame.hpp"
#include "core/detector.hpp"
#include "vision/color_detector.hpp"
#include "vision/edge_detector.hpp"
#include "vision/composite_detector.hpp"
#include "vision/primitives.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <print>

using namespace pixelbot;

// Always-on check: prints and returns failure count (accumulate for one retval).
static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::println(stderr, "FAIL: {} (line {})", #cond, __LINE__); ++g_failures; } \
} while (0)

// Helper to set a BGRA pixel in a frame buffer.
static void set_px(std::vector<std::byte>& buf, int w, int stride,
                   int x, int y, uint8_t b, uint8_t g, uint8_t r) {
    auto* p = reinterpret_cast<unsigned char*>(buf.data()) +
              static_cast<std::ptrdiff_t>(y) * stride + x * 4;
    p[0] = b; p[1] = g; p[2] = r; p[3] = 255;
}

// Fill a rectangle [x0,y0]-[x0+w,y0+h) with a solid BGRA color.
static void fill_rect(std::vector<std::byte>& buf, int /*w*/, int stride,
                      int rx, int ry, int rw, int rh,
                      uint8_t b, uint8_t g, uint8_t r) {
    for (int y = ry; y < ry + rh; ++y)
        for (int x = rx; x < rx + rw; ++x)
            set_px(buf, 0, stride, x, y, b, g, r);
}

// Build a DetectionConfig matching the sample profile's detection section,
// but with a red color range that catches pure red (H=0, S=255, V=255).
static DetectionConfig make_cfg(int fov) {
    DetectionConfig d{};
    d.technique = DetectionConfig::COMPOSITE;
    d.color_ranges.push_back({0, 5, 100, 255, 100, 255});
    d.fov_radius = fov;
    d.min_area = 12;
    d.head_offset = 3;
    d.hysteresis_distance = 30;
    d.hysteresis_factor = 0.8f;
    d.edge_threshold = 60;     // lower than config default so synthetic edges trip
    d.edge_min_aspect = 0.8f;
    return d;
}

// Make a 400x400 gray-background frame (stride == w*4, no padding).
static std::vector<std::byte> make_frame(int w, int h, int& stride_out) {
    stride_out = w * 4;
    std::vector<std::byte> buf(static_cast<size_t>(stride_out) * h, std::byte{0});
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            set_px(buf, w, stride_out, x, y, 128, 128, 128);
    return buf;
}

static int run_tests() {
    const int W = 400, H = 400;
    int stride;
    auto cfg = make_cfg(150);  // FOV radius 150 around center (200,200)

    // ---- Test 1: ColorDetector finds a 30x60 red rectangle off-center ------
    {
        auto buf = make_frame(W, H, stride);
        const int rx = 220, ry = 180, rw = 30, rh = 60;
        fill_rect(buf, W, stride, rx, ry, rw, rh, 0, 0, 255);
        Frame f{std::span<std::byte>{buf.data(), buf.size()}, W, H, stride, PixelFormat::BGRA8};

        ColorDetector det;
        Target t = det.detect(f, cfg);
        const int exp_x = rx + rw / 2;
        const int exp_y = ry + cfg.head_offset;
        std::println("T1 color: target={{{}, {}}} valid={} expected={{{}, {}}}",
                     t.x, t.y, t.valid, exp_x, exp_y);
        CHECK(t.valid);
        CHECK(std::abs(t.x - exp_x) <= 2);
        CHECK(std::abs(t.y - exp_y) <= 2);
        std::println("T1: ColorDetector found red 30x60 rect");
    }

    // ---- Test 2: EdgeDetector rejects a WIDE rectangle (aspect filter) ------
    {
        auto buf = make_frame(W, H, stride);
        const int rx = 210, ry = 190, rw = 60, rh = 15;
        fill_rect(buf, W, stride, rx, ry, rw, rh, 0, 0, 255);
        Frame f{std::span<std::byte>{buf.data(), buf.size()}, W, H, stride, PixelFormat::BGRA8};

        EdgeDetector det;
        Target t = det.detect(f, cfg);
        std::println("T2 edge-wide: target={{{}, {}}} valid={}", t.x, t.y, t.valid);
        CHECK(!t.valid);
        std::println("T2: EdgeDetector rejected wide rect (aspect filter)");
    }

    // ---- Test 2b: EdgeDetector ACCEPTS a tall rectangle ---------------------
    {
        auto buf = make_frame(W, H, stride);
        const int rx = 220, ry = 170, rw = 25, rh = 60;
        fill_rect(buf, W, stride, rx, ry, rw, rh, 0, 0, 255);
        Frame f{std::span<std::byte>{buf.data(), buf.size()}, W, H, stride, PixelFormat::BGRA8};

        EdgeDetector det;
        Target t = det.detect(f, cfg);
        std::println("T2b edge-tall: target={{{}, {}}} valid={}", t.x, t.y, t.valid);
        CHECK(t.valid);
        std::println("T2b: EdgeDetector accepted tall rect");
    }

    // ---- Test 3: CompositeDetector finds tall red rect (color AND edge) -----
    {
        auto buf = make_frame(W, H, stride);
        const int rx = 220, ry = 180, rw = 30, rh = 60;
        fill_rect(buf, W, stride, rx, ry, rw, rh, 0, 0, 255);
        Frame f{std::span<std::byte>{buf.data(), buf.size()}, W, H, stride, PixelFormat::BGRA8};

        CompositeDetector det;
        Target t = det.detect(f, cfg);
        const int exp_x = rx + rw / 2;
        const int exp_y = ry + cfg.head_offset;
        std::println("T3 composite-tall: target={{{}, {}}} valid={} expected={{{}, {}}}",
                     t.x, t.y, t.valid, exp_x, exp_y);
        CHECK(t.valid);
        CHECK(std::abs(t.x - exp_x) <= 2);
        CHECK(std::abs(t.y - exp_y) <= 2);
        std::println("T3: CompositeDetector found tall red rect");
    }

    // ---- Test 3b: CompositeDetector rejects a red HUD bar (wide, not tall) ---
    // A thin horizontal red bar: color matches and Sobel finds its top/bottom
    // horizontal edges, so color&edge fusion alone would accept it. The
    // aspect filter (shared with EdgeDetector) is what rejects it — the
    // "excellent discernment" property (a HUD bar isn't a tall player model).
    {
        auto buf = make_frame(W, H, stride);
        const int rx = 100, ry = 198, rw = 200, rh = 4;
        fill_rect(buf, W, stride, rx, ry, rw, rh, 0, 0, 255);
        Frame f{std::span<std::byte>{buf.data(), buf.size()}, W, H, stride, PixelFormat::BGRA8};

        CompositeDetector det;
        Target t = det.detect(f, cfg);
        std::println("T3b composite-hud: target={{{}, {}}} valid={}", t.x, t.y, t.valid);
        CHECK(!t.valid);
        std::println("T3b: CompositeDetector rejected red HUD bar (aspect filter)");
    }

    if (g_failures == 0) {
        std::println("\nALL TESTS PASSED");
        return 0;
    }
    std::println(stderr, "\n{} TEST(S) FAILED", g_failures);
    return 1;
}

int main() { return run_tests(); }
