// src/capture/window_finder.hpp — enumerate and locate X11 windows via EWMH.
//
// Uses _NET_CLIENT_LIST (all managed top-level windows, regardless of WM
// reparenting) + _NET_WM_NAME (UTF-8) with XFetchName fallback. Position is
// always via XTranslateCoordinates (XGetWindowAttributes.x/y is relative to
// the parent frame, not root, so it's wrong on reparenting WMs).
#pragma once
#include <X11/Xlib.h>
#include <string>
#include <vector>

namespace pixelbot {

struct WindowInfo {
    unsigned long id = 0;   // X11 Window ID
    std::string title;
    int x = 0, y = 0;       // on-screen origin (root coords)
    int w = 0, h = 0;       // client size (excludes border)
};

class WindowFinder {
public:
    WindowFinder();
    ~WindowFinder();
    WindowFinder(const WindowFinder&) = delete;
    WindowFinder& operator=(const WindowFinder&) = delete;

    // All managed, viewable top-level windows with a title (for the dropdown).
    [[nodiscard]] std::vector<WindowInfo> enumerate();

    // Re-query the on-screen geometry of a specific window (for tracking
    // movement/resize). Returns false if the window is gone.
    [[nodiscard]] bool geometry_of(unsigned long win, WindowInfo& out) const;

    // Find a window by title substring. Returns 0 if not found.
    [[nodiscard]] unsigned long find_by_title(const std::string& pattern);

    [[nodiscard]] Display* display() const noexcept { return dpy_; }

private:
    Display* dpy_ = nullptr;
};

} // namespace pixelbot
