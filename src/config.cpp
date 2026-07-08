// src/config.cpp — JSON parse + validation + atomic SIGHUP reload.
#include "config.hpp"
#include "error.hpp"
#include <nlohmann/json.hpp>
#include <csignal>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <utility>

namespace pixelbot {

// Signal-safe reload flag, set by the SIGHUP handler, consumed by the aim loop.
static volatile std::sig_atomic_t g_reload_requested = 0;
void ConfigStore::request_reload() noexcept { g_reload_requested = 1; }

const Profile& Config::active() const {
    auto it = profiles.find(active_profile);
    if (it == profiles.end())
        throw FatalError{"config: active_profile '" + active_profile +
                         "' not found in profiles"};
    return it->second;
}

ConfigStore& ConfigStore::instance() {
    static ConfigStore s;
    return s;
}

std::shared_ptr<const Config> ConfigStore::snapshot() const noexcept {
    return cfg_.load(std::memory_order_acquire);
}

bool ConfigStore::reload_if_requested() {
    if (!g_reload_requested) return false;
    g_reload_requested = 0;
    reload();
    return true;
}

void ConfigStore::install_sighup_handler() {
    struct sigaction sa{};
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = +[](int) { request_reload(); };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, nullptr);
}

// ---- parsing ----------------------------------------------------------------

using json = nlohmann::json;

static std::string read_file(const std::string& path) {
    std::ifstream f{path};
    if (!f) throw FatalError{"config: " + path + ": cannot open file"};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Require an integer key with bounds; throw on missing/wrong type/out-of-range.
static int req_int(const json& j, const std::string& path,
                   std::string_view key, int lo, int hi) {
    auto k = std::string{key};
    if (!j.contains(k)) throw FatalError{"config: " + path + ": missing '" + k + "'"};
    const auto& v = j.at(k);
    if (!v.is_number_integer())
        throw FatalError{"config: " + path + ": '" + k + "' must be an integer"};
    int val = v.get<int>();
    if (val < lo || val > hi)
        throw FatalError{"config: " + path + ": '" + k + "'=" +
                         std::to_string(val) + " out of range [" +
                         std::to_string(lo) + "," + std::to_string(hi) + "]"};
    return val;
}

static int req_pos_int(const json& j, const std::string& path,
                       std::string_view key) {
    return req_int(j, path, key, 1, std::numeric_limits<int>::max());
}

static float req_float(const json& j, const std::string& path,
                       std::string_view key, float lo, float hi) {
    auto k = std::string{key};
    if (!j.contains(k)) throw FatalError{"config: " + path + ": missing '" + k + "'"};
    const auto& v = j.at(k);
    if (!v.is_number())
        throw FatalError{"config: " + path + ": '" + k + "' must be a number"};
    float val = v.get<float>();
    if (!(val >= lo && val <= hi))
        throw FatalError{"config: " + path + ": '" + k + "'=" +
                         std::to_string(val) + " out of range [" +
                         std::to_string(lo) + "," + std::to_string(hi) + "]"};
    return val;
}

static std::string req_string(const json& j, const std::string& path,
                              std::string_view key) {
    auto k = std::string{key};
    if (!j.contains(k)) throw FatalError{"config: " + path + ": missing '" + k + "'"};
    const auto& v = j.at(k);
    if (!v.is_string())
        throw FatalError{"config: " + path + ": '" + k + "' must be a string"};
    return v.get<std::string>();
}

// Parse a [lo,hi] integer pair with bounds.
static std::pair<int,int> req_pair(const json& j, const std::string& path,
                                   std::string_view key, int lo, int hi) {
    auto k = std::string{key};
    if (!j.contains(k)) throw FatalError{"config: " + path + ": missing '" + k + "'"};
    const auto& v = j.at(k);
    if (!v.is_array() || v.size() != 2)
        throw FatalError{"config: " + path + ": '" + k + "' must be a 2-element array"};
    if (!v[0].is_number_integer() || !v[1].is_number_integer())
        throw FatalError{"config: " + path + ": '" + k + "' elements must be integers"};
    int a = v[0].get<int>(), b = v[1].get<int>();
    if (a < lo || a > hi || b < lo || b > hi)
        throw FatalError{"config: " + path + ": '" + k + "' element out of range [" +
                         std::to_string(lo) + "," + std::to_string(hi) + "]"};
    if (a > b)
        throw FatalError{"config: " + path + ": '" + k + "' first element > second"};
    return {a, b};
}

static ColorRange parse_color_range(const json& j, const std::string& path) {
    ColorRange r;
    auto [h0, h1] = req_pair(j, path, "h", 0, 180);
    auto [s0, s1] = req_pair(j, path, "s", 0, 255);
    auto [v0, v1] = req_pair(j, path, "v", 0, 255);
    r.h0 = h0; r.h1 = h1; r.s0 = s0; r.s1 = s1; r.v0 = v0; r.v1 = v1;
    return r;
}

static CaptureConfig parse_capture(const json& j, const std::string& path) {
    CaptureConfig c;
    c.region_x = req_int(j, path, "region_x", 0, std::numeric_limits<int>::max());
    c.region_y = req_int(j, path, "region_y", 0, std::numeric_limits<int>::max());
    c.region_w = req_pos_int(j, path, "region_w");
    c.region_h = req_pos_int(j, path, "region_h");
    return c;
}

static DetectionConfig parse_detection(const json& j, const std::string& path) {
    DetectionConfig d;
    std::string tech = req_string(j, path, "technique");
    if (tech == "color") d.technique = DetectionConfig::COLOR;
    else if (tech == "edge") d.technique = DetectionConfig::EDGE;
    else if (tech == "composite") d.technique = DetectionConfig::COMPOSITE;
    else throw FatalError{"config: " + path + ": unknown technique '" + tech + "'"};

    if (!j.contains("color_ranges"))
        throw FatalError{"config: " + path + ": missing 'color_ranges'"};
    const auto& cr = j.at("color_ranges");
    if (!cr.is_array())
        throw FatalError{"config: " + path + ": 'color_ranges' must be an array"};
    for (const auto& r : cr)
        d.color_ranges.push_back(parse_color_range(r, path));

    d.fov_radius = req_pos_int(j, path, "fov_radius");
    d.min_area = req_pos_int(j, path, "min_area");
    d.head_offset = req_int(j, path, "head_offset", 0, std::numeric_limits<int>::max());
    d.hysteresis_distance = req_float(j, path, "hysteresis_distance", 0.0f,
                                      static_cast<float>(std::numeric_limits<int>::max()));
    d.hysteresis_factor = req_float(j, path, "hysteresis_factor", 0.0f, 2.0f);
    d.edge_threshold = req_int(j, path, "edge_threshold", 0, 255);
    d.edge_min_aspect = req_float(j, path, "edge_min_aspect", 0.0f, 10.0f);
    return d;
}

static AimConfig parse_aim(const json& j, const std::string& path) {
    AimConfig a;
    a.x_sensitivity = req_float(j, path, "x_sensitivity", 0.0f, 10.0f);
    a.y_sensitivity = req_float(j, path, "y_sensitivity", 0.0f, 10.0f);
    a.smoothing = req_float(j, path, "smoothing", 0.0f, 1.0f);
    return a;
}

static Profile parse_profile(const json& j, const std::string& path) {
    Profile p;
    if (!j.contains("capture")) throw FatalError{"config: " + path + ": missing 'capture'"};
    if (!j.contains("detection")) throw FatalError{"config: " + path + ": missing 'detection'"};
    if (!j.contains("aim")) throw FatalError{"config: " + path + ": missing 'aim'"};
    p.capture = parse_capture(j.at("capture"), path + ".capture");
    p.detection = parse_detection(j.at("detection"), path + ".detection");
    p.aim = parse_aim(j.at("aim"), path + ".aim");
    return p;
}

static std::shared_ptr<const Config> parse_config(const std::string& text,
                                                  const std::string& path) {
    json j;
    try { j = json::parse(text); }
    catch (const json::parse_error& e) {
        throw FatalError{"config: " + path + ": JSON parse error: " + e.what()};
    }
    if (!j.is_object())
        throw FatalError{"config: " + path + ": root must be an object"};

    auto cfg = std::make_shared<Config>();
    cfg->active_profile = req_string(j, path, "active_profile");
    cfg->capture_backend = req_string(j, path, "capture_backend");
    cfg->input_backend = req_string(j, path, "input_backend");
    if (!j.contains("profiles"))
        throw FatalError{"config: " + path + ": missing 'profiles'"};
    const auto& profs = j.at("profiles");
    if (!profs.is_object())
        throw FatalError{"config: " + path + ": 'profiles' must be an object"};
    for (auto it = profs.begin(); it != profs.end(); ++it) {
        if (!it.value().is_object())
            throw FatalError{"config: " + path + ": profile '" + it.key() +
                             "' must be an object"};
        cfg->profiles.emplace(it.key(),
            parse_profile(it.value(), path + ".profiles." + it.key()));
    }
    // Validate active_profile exists (active() would throw, but fail early).
    if (cfg->profiles.find(cfg->active_profile) == cfg->profiles.end())
        throw FatalError{"config: " + path + ": active_profile '" +
                         cfg->active_profile + "' not in profiles"};
    return cfg;
}

void ConfigStore::load(const std::string& path) {
    path_ = path;
    auto cfg = parse_config(read_file(path), path);
    cfg_.store(std::move(cfg), std::memory_order_release);
}

void ConfigStore::reload() {
    if (path_.empty())
        throw FatalError{"config: reload called before load"};
    auto cfg = parse_config(read_file(path_), path_);
    cfg_.store(std::move(cfg), std::memory_order_release);
}

} // namespace pixelbot
