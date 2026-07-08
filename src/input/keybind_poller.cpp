// src/input/keybind_poller.cpp — X11 keybind polling for aim enable.
#include "keybind_poller.hpp"
#include "error.hpp"
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <cstring>
// Button1Mask..Button5Mask come from <X11/X.h> (via Xlib.h) as macros.

namespace pixelbot {

KeybindPoller::KeybindPoller(const KeybindConfig& cfg) : cfg_(cfg) {
    // Open our own X display for polling (independent of the capture backend's
    // connection, which runs on another thread).
    dpy_ = XOpenDisplay(nullptr);
    if (!dpy_)
        throw FatalError{"keybind: cannot open display (is DISPLAY set?)"};
    reconfigure(cfg_);
}

KeybindPoller::~KeybindPoller() {
    if (dpy_) XCloseDisplay(dpy_);
}

void KeybindPoller::reconfigure(const KeybindConfig& cfg) noexcept {
    cfg_ = cfg;
    prev_down_ = false;
    toggle_on_ = false;
    is_mouse_ = false;
    keycode_ = 0;
    button_mask_ = 0;

    if (cfg_.aim_key.empty()) {
        always_enabled_ = true;
        return;
    }
    always_enabled_ = false;

    // Mouse button: "Mouse1".."Mouse5".
    if (cfg_.aim_key.rfind("Mouse", 0) == 0 && cfg_.aim_key.size() == 6) {
        const int btn = cfg_.aim_key[5] - '0';
        if (btn >= 1 && btn <= 5) {
            is_mouse_ = true;
            switch (btn) {
                case 1: button_mask_ = Button1Mask; break;
                case 2: button_mask_ = Button2Mask; break;
                case 3: button_mask_ = Button3Mask; break;
                case 4: button_mask_ = Button4Mask; break;
                case 5: button_mask_ = Button5Mask; break;
            }
            return;
        }
    }

    // Keyboard: resolve a keysym string (e.g. "space", "F1", "Caps_Lock") to
    // a KeyCode. Unknown keysym → always_enabled stays false, aim won't fire;
    // that's a config usability bug, not a crash (the GUI will surface it).
    const KeySym ks = XStringToKeysym(const_cast<char*>(cfg_.aim_key.c_str()));
    if (ks != NoSymbol && dpy_) {
        keycode_ = XKeysymToKeycode(dpy_, ks);
    }
}

bool KeybindPoller::key_down() const noexcept {
    if (always_enabled_) return true;

    if (is_mouse_) {
        ::Window root, child;
        int rx, ry, wx, wy;
        unsigned int mask = 0;
        XQueryPointer(dpy_, DefaultRootWindow(dpy_), &root, &child,
                      &rx, &ry, &wx, &wy, &mask);
        return (mask & button_mask_) != 0;
    }

    if (keycode_ == 0) return false;
    // XQueryKeymap returns a 32-byte bitmap of currently-down keycodes.
    char keys[32];
    XQueryKeymap(dpy_, keys);
    return (keys[keycode_ / 8] & (1 << (keycode_ % 8))) != 0;
}

void KeybindPoller::handle_toggle_edge(bool down) noexcept {
    if (cfg_.aim_mode != "toggle") return;
    // Edge: false → true is a press.
    if (down && !prev_down_) toggle_on_ = !toggle_on_;
    prev_down_ = down;
}

bool KeybindPoller::aim_enabled() noexcept {
    if (always_enabled_) return true;
    if (cfg_.aim_mode == "toggle") {
        const bool down = key_down();
        handle_toggle_edge(down);
        return toggle_on_;
    }
    // hold mode
    return key_down();
}

} // namespace pixelbot
