// src/capture/x11_capture.cpp — Xlib + MIT-SHM capture with window tracking.
//
// Always allocates a full-screen SHM buffer (so window resize/move never needs
// reallocation). capture() grabs the entire root window via XShmGetImage, then
// returns a Frame sub-view offset to the target window's current geometry (or
// the manual region if no window is set). The detector/aim code already uses
// stride correctly, so a sub-view with full-screen stride works seamlessly.
#include "x11_capture.hpp"
#include "error.hpp"
#include <X11/Xutil.h>
#include <cstddef>
#include <span>
#include <sys/ipc.h>
#include <cstring>
#include <print>

namespace pixelbot {

X11Capture::X11Capture(const CaptureConfig& cfg) : cfg_(cfg) {
    try {
        dpy_ = XOpenDisplay(nullptr);
        if (!dpy_)
            throw FatalError{"x11: cannot open display (is DISPLAY set?)"};
        if (!XShmQueryExtension(dpy_))
            throw FatalError{"x11: MIT-SHM not supported by this X server"};
        XInitThreads();

        const int screen = DefaultScreen(dpy_);
        const unsigned int depth = static_cast<unsigned int>(DefaultDepth(dpy_, screen));
        if (depth < 24)
            throw FatalError{"x11: root depth < 24 (only 24/32-bit TrueColor supported)"};

        // Full-screen dimensions — the SHM buffer always covers the entire root
        // so any window sub-region fits without reallocation.
        screen_w_ = DisplayWidth(dpy_, screen);
        screen_h_ = DisplayHeight(dpy_, screen);

        // Allocate SHM for one full-screen BGRA8 frame.
        const std::size_t bytes = static_cast<std::size_t>(screen_w_) *
                                  static_cast<std::size_t>(screen_h_) * 4u;
        shmid_ = shmget(IPC_PRIVATE, static_cast<std::size_t>(bytes), IPC_CREAT | 0777);
        if (shmid_ == -1)
            throw FatalError{"x11: shmget failed"};
        shmaddr_ = static_cast<char*>(shmat(shmid_, nullptr, 0));
        if (shmaddr_ == reinterpret_cast<char*>(-1))
            throw FatalError{"x11: shmat failed"};

        shminfo_.shmid = shmid_;
        shminfo_.shmaddr = shmaddr_;
        shminfo_.readOnly = False;

        if (!XShmAttach(dpy_, &shminfo_))
            throw FatalError{"x11: XShmAttach failed"};
        attached_ = true;

        // XShmCreateImage adopts `shmaddr_` as its data buffer. ZPixmap layout
        // is B,G,R,X in memory (little-endian) on a typical 24/32-bit X server;
        // we treat it as BGRA8 (alpha = padding byte).
        img_ = XShmCreateImage(dpy_, DefaultVisual(dpy_, screen), depth,
                               ZPixmap, shmaddr_, &shminfo_,
                               static_cast<unsigned int>(screen_w_),
                               static_cast<unsigned int>(screen_h_));
        if (!img_)
            throw FatalError{"x11: XShmCreateImage failed"};
        if (img_->bits_per_pixel != 32)
            throw FatalError{"x11: expected 32 bits/pixel ZPixmap, got " +
                             std::to_string(img_->bits_per_pixel)};

        // Seed current dimensions from the initial config.
        target_win_ = cfg_.window_id;
        cur_w_ = cfg_.region_w;
        cur_h_ = cfg_.region_h;
        if (target_win_ != 0) {
            int x, y, w, h;
            if (query_window_geom(x, y, w, h)) { cur_w_ = w; cur_h_ = h; }
        }
    } catch (...) {
        cleanup();
        throw;
    }
}

bool X11Capture::query_window_geom(int& x, int& y, int& w, int& h) const noexcept {
    XWindowAttributes attr;
    if (XGetWindowAttributes(dpy_, static_cast<Window>(target_win_), &attr) == 0)
        return false;
    Window child;
    if (XTranslateCoordinates(dpy_, static_cast<Window>(target_win_),
                              DefaultRootWindow(dpy_), 0, 0, &x, &y, &child) == 0)
        return false;
    w = attr.width;
    h = attr.height;
    // Clamp to screen bounds so the sub-view doesn't read past the SHM buffer.
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > screen_w_) w = screen_w_ - x;
    if (y + h > screen_h_) h = screen_h_ - y;
    return w > 0 && h > 0;
}

Frame X11Capture::capture() {
    // Window-targeted: capture the window drawable directly via XGetImage.
    // This is required for Vulkan games under Xwayland — the root window is
    // black because the compositor doesn't composite Vulkan surfaces into it.
    // XGetImage on the window itself returns the rendered content.
    if (target_win_ != 0) {
        int wx, wy, ww, wh;
        if (query_window_geom(wx, wy, ww, wh)) {
            // Free the previous frame's XImage (the pipeline has already
            // copied it into a FrameSnap by now).
            if (win_img_) { win_img_->data = nullptr; win_img_->f.destroy_image(win_img_); win_img_ = nullptr; }
            win_img_ = XGetImage(dpy_, static_cast<Window>(target_win_),
                                 0, 0, static_cast<unsigned int>(ww),
                                 static_cast<unsigned int>(wh),
                                 AllPlanes, ZPixmap);
            if (!win_img_)
                throw FatalError{"x11: XGetImage on target window failed"};
            cur_w_ = win_img_->width;
            cur_h_ = win_img_->height;
            return Frame{
                .data = std::span<std::byte>{
                    reinterpret_cast<std::byte*>(win_img_->data),
                    static_cast<std::size_t>(win_img_->bytes_per_line) * win_img_->height},
                .width = win_img_->width,
                .height = win_img_->height,
                .stride = win_img_->bytes_per_line,
                .format = PixelFormat::BGRA8,
            };
        }
        // Window gone — fall through to root capture.
    }

    // Manual region: capture the full root window into the SHM buffer.
    if (!XShmGetImage(dpy_, DefaultRootWindow(dpy_), img_, 0, 0, AllPlanes))
        throw FatalError{"x11: XShmGetImage failed"};
    cur_w_ = screen_w_;
    cur_h_ = screen_h_;
    return Frame{
        .data = std::span<std::byte>{reinterpret_cast<std::byte*>(shmaddr_),
            static_cast<std::size_t>(img_->bytes_per_line) * screen_h_},
        .width = screen_w_,
        .height = screen_h_,
        .stride = img_->bytes_per_line,
        .format = PixelFormat::BGRA8,
    };
}

int X11Capture::width() const noexcept {
    if (target_win_ != 0) return cur_w_ > 0 ? cur_w_ : cfg_.region_w;
    return cfg_.region_w > 0 ? cfg_.region_w : screen_w_;
}

int X11Capture::height() const noexcept {
    if (target_win_ != 0) return cur_h_ > 0 ? cur_h_ : cfg_.region_h;
    return cfg_.region_h > 0 ? cfg_.region_h : screen_h_;
}

X11Capture::~X11Capture() { cleanup(); }

void X11Capture::cleanup() noexcept {
    if (img_) {
        img_->data = nullptr;
        XDestroyImage(img_);
        img_ = nullptr;
    }
    if (win_img_) { win_img_->data = nullptr; win_img_->f.destroy_image(win_img_); win_img_ = nullptr; }
    if (attached_ && dpy_) { XShmDetach(dpy_, &shminfo_); attached_ = false; }
    if (shmaddr_ && shmaddr_ != reinterpret_cast<char*>(-1)) { shmdt(shmaddr_); shmaddr_ = nullptr; }
    if (shmid_ != -1) { shmctl(shmid_, IPC_RMID, nullptr); shmid_ = -1; }
    if (dpy_) { XCloseDisplay(dpy_); dpy_ = nullptr; }
}

} // namespace pixelbot
