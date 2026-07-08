// src/pipeline.hpp — capture + aim threads over a shared latest-frame snapshot.
#pragma once
#include "core/capture.hpp"
#include "core/detector.hpp"
#include "core/mouse.hpp"
#include "core/frame.hpp"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include "input/keybind_poller.hpp"
#include <cstddef>

namespace pixelbot {

// A copied frame: the SHM capture buffer is single, so the capture thread
// copies the latest frame's bytes here before the next capture() call. The aim
// thread reads a shared_ptr snapshot (atomic, spinlock-serialized on this
// libstdc++ — not lock-free, but contention-free for one reader/writer) and
// builds a Frame view over the snap's owned bytes.
struct FrameSnap {
    int width = 0, height = 0, stride = 0;
    PixelFormat format = PixelFormat::BGRA8;
    std::vector<std::byte> data;
};

class Pipeline {
public:
    // `mouse` may be nullptr when dry_run is true (no input injection).
    Pipeline(std::unique_ptr<CaptureBackend> capture,
             std::unique_ptr<Detector> detector,
             std::unique_ptr<MouseInput> mouse,
             bool dry_run);
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void start();
    void stop();

private:
    void capture_loop(std::stop_token st);
    void aim_loop(std::stop_token st);

    std::unique_ptr<CaptureBackend> capture_;
    std::unique_ptr<Detector> detector_;
    std::unique_ptr<MouseInput> mouse_;
    std::unique_ptr<KeybindPoller> keybind_;
    KeybindConfig last_keybind_;
    bool dry_run_;
    std::atomic<std::shared_ptr<FrameSnap>> latest_;
    std::jthread capture_thread_, aim_thread_;
};

} // namespace pixelbot
