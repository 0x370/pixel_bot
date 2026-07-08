// src/capture/x11_capture.hpp — Xlib + MIT-SHM capture backend.
//
// Two modes:
//   - Manual region: capture a fixed sub-rectangle of the root window
//     (region_x/y/w/h from config).
//   - Window tracking: capture the full root window into a screen-sized SHM
//     buffer and sub-view the Frame to the target window's current geometry.
//     The window's position/size is re-queried each capture() call, so it
//     tracks movement and resize without SHM reallocation.
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
    [[nodiscard]] int width() const noexcept override;
    [[nodiscard]] int height() const noexcept override;

    // Set the window to track (0 = revert to manual region mode).
    void set_target_window(unsigned long win) noexcept { target_win_ = win; }
    [[nodiscard]] unsigned long target_window() const noexcept { return target_win_; }

private:
    CaptureConfig cfg_;
    Display* dpy_ = nullptr;
    XShmSegmentInfo shminfo_{};
    XImage* img_ = nullptr;
    char* shmaddr_ = nullptr;
    int shmid_ = -1;
    bool attached_ = false;
    unsigned long target_win_ = 0;   // 0 = manual region; >0 = track this window
    int screen_w_ = 0, screen_h_ = 0;
    int cur_w_ = 0, cur_h_ = 0;
    // Non-SHM XImage for window-targeted captures (XGetImage allocates per
    // call; we hold it so the Frame stays valid until the next capture()).
    XImage* win_img_ = nullptr;
    void cleanup() noexcept;
    // Query the target window's on-screen geometry. Returns false if gone.
    bool query_window_geom(int& x, int& y, int& w, int& h) const noexcept;
};

} // namespace pixelbot
