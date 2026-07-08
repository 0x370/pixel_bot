// src/pipeline.cpp — capture + aim threads with atomic latest-frame handoff.
//
// Capture thread: capture() → copy bytes into a new FrameSnap → atomically
//   store as shared_ptr (latest-wins; drops frames the aim thread can't keep
//   up with — no backpressure, no queue).
// Aim thread: load snapshot → load config snapshot → detect → translate to
//   center-relative coords → compute smoothed delta → move mouse (or log in
//   dry-run) → pace ~1kHz.
#include "pipeline.hpp"
#include "config.hpp"
#include "aim/aim_controller.hpp"
#include <print>
#include <thread>
#include <chrono>
#include <cmath>
#include <span>

namespace pixelbot {

Pipeline::Pipeline(std::unique_ptr<CaptureBackend> capture,
                   std::unique_ptr<Detector> detector,
                   std::unique_ptr<MouseInput> mouse,
                   bool dry_run)
    : capture_(std::move(capture)),
      detector_(std::move(detector)),
      mouse_(std::move(mouse)),
      dry_run_(dry_run) {}

Pipeline::~Pipeline() { stop(); }

void Pipeline::start() {
    capture_->start();
    // Build the keybind poller from the current config (opens its own X
    // display for polling). If it can't open the display (e.g. no X), aim
    // stays disabled — not fatal, since --once / headless runs don't need it.
    if (auto cfg = ConfigStore::instance().snapshot()) {
        try {
            keybind_ = std::make_unique<KeybindPoller>(cfg->active().keybind);
            last_keybind_ = cfg->active().keybind;
        } catch (const FatalError& e) {
            std::println(stderr, "pixelbot: {}", e.what());
            std::println(stderr, "  (aim will be disabled — no keybind polling)");
        }
    }
    capture_thread_ = std::jthread([this](std::stop_token st) { capture_loop(st); });
    aim_thread_     = std::jthread([this](std::stop_token st) { aim_loop(st); });
}

void Pipeline::stop() {
    if (capture_thread_.joinable()) capture_thread_.request_stop();
    if (aim_thread_.joinable())     aim_thread_.request_stop();
    if (capture_thread_.joinable()) capture_thread_.join();
    if (aim_thread_.joinable())     aim_thread_.join();
    if (capture_) capture_->stop();
}

void Pipeline::capture_loop(std::stop_token st) {
    unsigned long last_win = 0;
    while (!st.stop_requested()) {
        // Check if the GUI changed the target window (published via config).
        if (auto cfg = ConfigStore::instance().snapshot()) {
            unsigned long w = cfg->active().capture.window_id;
            if (w != last_win) {
                capture_->set_target_window(w);
                last_win = w;
            }
        }
        Frame f = capture_->capture();
        auto snap = std::make_shared<FrameSnap>();
        snap->width  = f.width;
        snap->height = f.height;
        snap->stride = f.stride;
        snap->format = f.format;
        // Copy the frame bytes out of the single SHM buffer before the next
        // capture() call reuses it. latest-wins: a slow aim thread simply
        // drops frames (no backpressure, no queue).
        snap->data.assign(f.data.begin(), f.data.end());
        latest_.store(std::move(snap), std::memory_order_release);
        // Pace: the aim loop runs ~1kHz; capture spinning faster than that
        // only thrashes the allocator with FrameSnap copies nobody reads.
        // A short sleep keeps capture ahead of aim without burning a core.
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

void Pipeline::aim_loop(std::stop_token st) {
    AimController aim;
    while (!st.stop_requested()) {
        // Live config reload (SIGHUP sets the flag; this consumes it on the
        // main thread where exceptions are well-formed).
        ConfigStore::instance().reload_if_requested();
        auto cfg = ConfigStore::instance().snapshot();
        if (!cfg) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }

        auto snap = latest_.load(std::memory_order_acquire);
        if (!snap) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }

        // Build a Frame view over the snap's owned bytes (snap stays alive via
        // the shared_ptr for the duration of this iteration).
        Frame f{
            .data = std::span<std::byte>{snap->data.data(), snap->data.size()},
            .width = snap->width,
            .height = snap->height,
            .stride = snap->stride,
            .format = snap->format,
        };

        const auto& prof = cfg->active();
        Target t = detector_->detect(f, prof.detection);

        // Translate to screen-center-relative coords for the aim controller.
        t.x -= f.width / 2;
        t.y -= f.height / 2;

        Delta d = aim.compute(t, prof.aim);

        // Reconfigure the keybind poller if the keybind changed (GUI publish).
        if (keybind_) {
            const auto& new_kb = prof.keybind;
            if (new_kb.aim_key != last_keybind_.aim_key ||
                new_kb.aim_mode != last_keybind_.aim_mode) {
                keybind_->reconfigure(new_kb);
                last_keybind_ = new_kb;
            }
        }

        // Gate mouse movement on the aim keybind (hold/toggle). In dry-run we
        // log regardless so the user can see detection working.
        const bool aim_on = keybind_ ? keybind_->aim_enabled() : true;

        if (dry_run_) {
            std::println(stderr, "target={{{}, {}}} delta={{{:.1f}, {:.1f}}} aim={}",
                         t.x, t.y, d.dx, d.dy, aim_on ? "ON" : "off");
        } else if (mouse_ && aim_on) {
            mouse_->move(static_cast<int>(std::lround(d.dx)),
                         static_cast<int>(std::lround(d.dy)));
        }

        // Pace to ~1kHz; prevents a busy spin if capture stalls.
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
    }
}

} // namespace pixelbot
