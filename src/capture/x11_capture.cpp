// src/capture/x11_capture.cpp — Xlib + MIT-SHM synchronous capture.
//
// Reads the configured sub-rectangle of the root window directly into a
// shared-memory segment via XShmGetImage. The Frame returned by capture()
// aliases the SHM buffer (zero-copy); the consumer must finish reading before
// the next capture() call (single-buffer latest-frame contract). stride is
// read from img->bytes_per_line, never assumed == width*4 (X11 may pad).
#include "x11_capture.hpp"
#include <X11/Xutil.h>
#include "error.hpp"
#include <cstddef>
#include <span>
#include <sys/ipc.h>
#include <cstring>

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

        // Allocate the SHM segment for one BGRA8 frame of the capture region.
        const std::size_t bytes = static_cast<std::size_t>(cfg_.region_w) *
                                  static_cast<std::size_t>(cfg_.region_h) * 4u;
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

        // XShmCreateImage adopts `shmaddr_` as its data buffer. On a typical
        // 24/32-bit TrueColor X server the ZPixmap layout is B,G,R,X in memory
        // (little-endian), which we treat as BGRA8 (alpha = padding byte).
        img_ = XShmCreateImage(dpy_, DefaultVisual(dpy_, screen), depth,
                               ZPixmap, shmaddr_, &shminfo_,
                               static_cast<unsigned int>(cfg_.region_w),
                               static_cast<unsigned int>(cfg_.region_h));
        if (!img_)
            throw FatalError{"x11: XShmCreateImage failed"};
        if (img_->bits_per_pixel != 32)
            throw FatalError{"x11: expected 32 bits/pixel ZPixmap, got " +
                             std::to_string(img_->bits_per_pixel)};
    } catch (...) {
        // A throwing ctor never reaches ~X11Capture; release partial work.
        cleanup();
        throw;
    }
}

Frame X11Capture::capture() {
    if (!XShmGetImage(dpy_, DefaultRootWindow(dpy_), img_,
                      cfg_.region_x, cfg_.region_y, AllPlanes))
        throw FatalError{"x11: XShmGetImage failed"};
    return Frame{
        .data = std::span<std::byte>{reinterpret_cast<std::byte*>(shmaddr_),
            static_cast<std::size_t>(img_->bytes_per_line) * img_->height},
        .width = img_->width,
        .height = img_->height,
        .stride = img_->bytes_per_line,
        .format = PixelFormat::BGRA8,
    };
}

X11Capture::~X11Capture() { cleanup(); }

void X11Capture::cleanup() noexcept {
    // XDestroyImage's default impl calls Xfree(image->data). For an SHM-backed
    // image img_->data == shmaddr_; nulling it first makes the free a no-op,
    // so we own the segment and unmap it via shmdt ourselves — no double-free.
    if (img_) {
        img_->data = nullptr;
        XDestroyImage(img_);
        img_ = nullptr;
    }
    if (attached_ && dpy_) { XShmDetach(dpy_, &shminfo_); attached_ = false; }
    if (shmaddr_ && shmaddr_ != reinterpret_cast<char*>(-1)) { shmdt(shmaddr_); shmaddr_ = nullptr; }
    if (shmid_ != -1) { shmctl(shmid_, IPC_RMID, nullptr); shmid_ = -1; }
    if (dpy_) { XCloseDisplay(dpy_); dpy_ = nullptr; }
}

} // namespace pixelbot
