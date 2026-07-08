// src/config.hpp — JSON config structs + atomic snapshot store.
//
// Loaded at startup, reloadable on SIGHUP. The hot loop snapshots
// std::shared_ptr<const Config> each iteration (atomic read; on this libstdc++
// std::atomic<shared_ptr> is spinlock-serialized, not lock-free, but contention-
// free for one reader/writer). Schema is fixed (see plan step 3); implementers
// must not invent keys.
#pragma once
#include "error.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pixelbot {

struct ColorRange {
    int h0=0, h1=0, s0=0, s1=0, v0=0, v1=0;
};

struct DetectionConfig {
    enum Technique { COLOR, EDGE, COMPOSITE };
    Technique technique = COMPOSITE;
    std::vector<ColorRange> color_ranges;
    int fov_radius = 0;
    int min_area = 0;
    int head_offset = 0;
    float hysteresis_distance = 0.0f;
    float hysteresis_factor = 0.0f;
    int edge_threshold = 0;
    float edge_min_aspect = 0.0f;
};

struct AimConfig {
    float x_sensitivity = 0.0f;
    float y_sensitivity = 0.0f;
    float smoothing = 0.0f;
};

struct CaptureConfig {
    int region_x = 0, region_y = 0, region_w = 0, region_h = 0;
    unsigned long window_id = 0;  // 0 = use region_x/y/w/h; >0 = track this X window
};

// Per-profile keybind for enabling aiming. `aim_key` is an X11 keysym name
// (e.g. "F1", "space", "Caps_Lock") or "Mouse1".."Mouse5". Empty = aim always
// enabled (no keybind). `aim_mode`: "hold" (aim while key down) or "toggle"
// (press to flip on/off).
struct KeybindConfig {
    std::string aim_key;
    std::string aim_mode = "hold";
};

struct Profile {
    CaptureConfig capture;
    DetectionConfig detection;
    AimConfig aim;
    KeybindConfig keybind;
};

struct Config {
    std::string active_profile;
    std::string capture_backend;
    std::string input_backend;
    std::unordered_map<std::string, Profile> profiles;
    // Returns the active profile; throws FatalError if active_profile is unset
    // or names a missing profile.
    [[nodiscard]] const Profile& active() const;
};

// Atomic snapshot store. load() builds the initial config from a file;
// reload() re-reads the same path and atomically swaps in a new Config
// (atomic swap; on this libstdc++ std::atomic<shared_ptr> is spinlock-
// serialized, not lock-free). snapshot() returns a shared_ptr copy that is
// safe to use for as long as the caller holds it.
//
// SIGHUP safety: file I/O + JSON parse + throwing FatalError inside a signal
// handler is UB (throwing across a signal frame; non-signal-safe calls). So
// the SIGHUP handler only sets an atomic flag (signal-safe); the aim loop
// calls reload_if_requested() on the main thread where exceptions are
// well-formed. This is the safe realization of the plan's "SIGHUP -> reload".
class ConfigStore {
public:
    static ConfigStore& instance();
    // Build the initial config from `path` and store it. Throws FatalError on
    // any parse/validation failure. Remembers `path` for later reload().
    void load(const std::string& path);
    // Re-read the remembered path and atomically swap. Throws FatalError on
    // failure (old config is retained on failure).
    void reload();
    // If the SIGHUP flag is set, clear it and call reload(). Returns true if a
    // reload was performed.
    bool reload_if_requested();
    // Lock-free snapshot of the current config.
    [[nodiscard]] std::shared_ptr<const Config> snapshot() const noexcept;
    // Atomically publish a new config (used by the GUI to push live edits).
    // The aim loop picks it up on the next snapshot read.
    void publish(std::shared_ptr<const Config> cfg) noexcept;
    // Mark reload requested (called from the SIGHUP handler).
    static void request_reload() noexcept;
    // Install the SIGHUP handler that sets the reload flag.
    static void install_sighup_handler();
private:
    ConfigStore() = default;
    std::atomic<std::shared_ptr<const Config>> cfg_;
    std::string path_;
};

} // namespace pixelbot
