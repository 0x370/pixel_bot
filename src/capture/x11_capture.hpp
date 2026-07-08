// src/capture/x11_capture.hpp — Xlib + MIT-SHM capture backend.
#pragma once
#include "core/capture.hpp"
#include "config.hpp"
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>

namespace pixelbot {

class X11Capture : public CaptureBackend {
public:
    explicit X11Capture(const CaptureConfig& cfg);
    ~X11Capture() override;
    X11Capture(const X11Capture&) = delete;
    X11Capture& operator=(const X11Capture&) = delete;

    void start() override {}   // X11 SHM is synchronous; no setup beyond ctor.
    void stop() override {}
    Frame capture() override;
    [[nodiscard]] int width() const noexcept override { return cfg_.region_w; }
    [[nodiscard]] int height() const noexcept override { return cfg_.region_h; }

private:
    CaptureConfig cfg_;
    Display* dpy_ = nullptr;
    XShmSegmentInfo shminfo_{};
    XImage* img_ = nullptr;
    char* shmaddr_ = nullptr;
    int shmid_ = -1;
    bool attached_ = false;
    void cleanup() noexcept;  // release all owned resources (idempotent).
};

} // namespace pixelbot
