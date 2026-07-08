// src/capture/window_finder.cpp — EWMH window enumeration + geometry.
#include "window_finder.hpp"
#include "error.hpp"
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <algorithm>
#include <cstring>
#include <print>

namespace pixelbot {

WindowFinder::WindowFinder() {
    dpy_ = XOpenDisplay(nullptr);
    if (!dpy_)
        throw FatalError{"window_finder: cannot open display (is DISPLAY set?)"};
}

WindowFinder::~WindowFinder() {
    if (dpy_) XCloseDisplay(dpy_);
}

// Read _NET_WM_NAME (UTF-8) with XFetchName (Latin-1) fallback.
static std::string get_window_title(Display* dpy, Window win) {
    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(dpy, win, net_wm_name, 0, 1024, False, utf8,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &data) == Success && data) {
        std::string title{reinterpret_cast<char*>(data), nitems};
        XFree(data);
        return title;
    }
    // Fallback: legacy WM_NAME (Latin-1 / compound text).
    char* name = nullptr;
    if (XFetchName(dpy, win, &name) && name) {
        std::string title{name};
        XFree(name);
        return title;
    }
    return {};
}

// Get on-screen geometry via XTranslateCoordinates (position) +
// XGetWindowAttributes (size). XGetWindowAttributes.x/y is relative to parent
// frame on reparenting WMs — NOT the screen position.
static bool get_geometry(Display* dpy, Window win, int& x, int& y,
                         int& w, int& h) {
    XWindowAttributes attr;
    if (XGetWindowAttributes(dpy, win, &attr) == 0) return false;
    if (attr.map_state != IsViewable) return false;
    Window child;
    if (XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy),
                              0, 0, &x, &y, &child) == 0) return false;
    w = attr.width;
    h = attr.height;
    return true;
}

// Recursively walk the window tree (fallback for WMs without _NET_CLIENT_LIST,
// e.g. Xwayland rootless). Collects all viewable windows with a title.
static void walk_tree(Display* dpy, Window win, std::vector<WindowInfo>& out) {
    Window root, parent; Window* children = nullptr; unsigned int n = 0;
    if (XQueryTree(dpy, win, &root, &parent, &children, &n) == 0 || n == 0) {
        if (children) XFree(children);
        return;
    }
    for (unsigned int i = 0; i < n; ++i) {
        Window w = children[i];
        std::string title = get_window_title(dpy, w);
        int x, y, ww, hh;
        bool visible = get_geometry(dpy, w, x, y, ww, hh);
        if (visible && !title.empty()) {
            out.push_back(WindowInfo{
                .id = static_cast<unsigned long>(w),
                .title = std::move(title),
                .x = x, .y = y, .w = ww, .h = hh});
        }
        // Recurse — the actual client may be nested inside a WM frame.
        walk_tree(dpy, w, out);
    }
    if (children) XFree(children);
}

std::vector<WindowInfo> WindowFinder::enumerate() {
    std::vector<WindowInfo> result;

    // --- Path 1: EWMH _NET_CLIENT_LIST (preferred; works on full WMs) ---
    Atom net_client_list = XInternAtom(dpy_, "_NET_CLIENT_LIST", False);
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(dpy_, DefaultRootWindow(dpy_), net_client_list,
                           0, (~0L), False, XA_WINDOW,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &data) == Success && data && nitems > 0) {
        auto* wins = reinterpret_cast<Window*>(data);
        for (unsigned long i = 0; i < nitems; ++i) {
            WindowInfo info;
            info.id = static_cast<unsigned long>(wins[i]);
            info.title = get_window_title(dpy_, wins[i]);
            int x, y, w, h;
            if (!get_geometry(dpy_, wins[i], x, y, w, h)) continue;
            info.x = x; info.y = y; info.w = w; info.h = h;
            if (info.title.empty()) continue;
            result.push_back(std::move(info));
        }
        XFree(data);
    } else {
        if (data) XFree(data);
        // --- Path 2: XQueryTree recursion (fallback; Xwayland, no-EWMH WMs) ---
        walk_tree(dpy_, DefaultRootWindow(dpy_), result);
    }

    // Sort by title for a stable dropdown.
    std::sort(result.begin(), result.end(),
              [](const WindowInfo& a, const WindowInfo& b) { return a.title < b.title; });
    return result;
}

bool WindowFinder::geometry_of(unsigned long win, WindowInfo& out) const {
    out.id = win;
    int x, y, w, h;
    if (!get_geometry(dpy_, static_cast<Window>(win), x, y, w, h)) return false;
    out.x = x; out.y = y; out.w = w; out.h = h;
    // Title is not needed for geometry tracking; leave it.
    return true;
}

unsigned long WindowFinder::find_by_title(const std::string& pattern) {
    auto wins = enumerate();
    for (const auto& w : wins) {
        // Case-insensitive substring match.
        if (w.title.find(pattern) != std::string::npos)
            return w.id;
    }
    return 0;
}

} // namespace pixelbot
