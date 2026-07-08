// src/input/keybind_poller.hpp — poll keyboard/mouse state via X11.
//
// Reads keybind config (aim_key + aim_mode) from the active profile and reports
// whether aiming is currently enabled. Two modes:
//   hold   — aim enabled while the bound key/button is held down.
//   toggle — each press flips enabled state (edge-triggered).
//
// Uses XQueryKeymap (keyboard) + XQueryPointer button mask (mouse buttons).
// Empty aim_key → always enabled (no keybind).
#pragma once
#include "config.hpp"
#include <X11/Xlib.h>
#include <atomic>

namespace pixelbot {

class KeybindPoller {
public:
    explicit KeybindPoller(const KeybindConfig& cfg);
    ~KeybindPoller();
    KeybindPoller(const KeybindPoller&) = delete;
    KeybindPoller& operator=(const KeybindPoller&) = delete;

    // Reconfigure the keybind (called by the GUI on publish). Resets edge state.
    void reconfigure(const KeybindConfig& cfg) noexcept;

    // Returns true if aiming is enabled right now. The aim loop calls this each
    // iteration; for toggle mode the edge transition is tracked internally.
    [[nodiscard]] bool aim_enabled() noexcept;

private:
    bool key_down() const noexcept;
    void handle_toggle_edge(bool down) noexcept;

    Display* dpy_ = nullptr;
    KeybindConfig cfg_;
    // For toggle mode: latches the previous physical state to detect edges.
    bool prev_down_ = false;
    bool toggle_on_ = false;
    // If true, aim_key is a mouse button ("Mouse1".."Mouse5"); else a keysym.
    bool is_mouse_ = false;
    // KeyCode for keyboard keys; resolved from the keysym string once.
    unsigned int keycode_ = 0;
    // Button mask bit for mouse buttons (Button1Mask..Button5Mask).
    unsigned int button_mask_ = 0;
    // Empty aim_key → always enabled.
    bool always_enabled_ = false;
};

} // namespace pixelbot
