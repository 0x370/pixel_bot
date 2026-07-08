// src/gui/gui.cpp — Dear ImGui + SDL2 control panel.
//
// Renders sliders/toggles for detection + aim + keybind, a keybind-capture
// editor, and publishes edited config to ConfigStore on change. Runs on the
// main thread; the pipeline threads read snapshots lock-free.
#include "gui.hpp"
#include "config.hpp"
#include "error.hpp"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_mouse.h>
#include <print>
#include <cstring>
#include <string>
#include <vector>

namespace pixelbot {

// Deep-clone a Config (shared_ptr<const Config> → mutable Config).
static Config clone_config(const Config& src) {
    Config c;
    c.active_profile = src.active_profile;
    c.capture_backend = src.capture_backend;
    c.input_backend = src.input_backend;
    c.profiles = src.profiles;
    return c;
}

// Translate an SDL scancode to an X11 keysym name string for KeybindConfig.
static std::string scancode_to_keysym_name(SDL_Scancode sc) {
    // SDL scancodes map cleanly to the common physical keys; use the SDL
    // name where it matches the X11 keysym name, else fall back to the SDL
    // scancode name. The keybind poller resolves these via XStringToKeysym.
    switch (sc) {
        case SDL_SCANCODE_SPACE:    return "space";
        case SDL_SCANCODE_RETURN:   return "Return";
        case SDL_SCANCODE_TAB:      return "Tab";
        case SDL_SCANCODE_ESCAPE:   return "Escape";
        case SDL_SCANCODE_BACKSPACE:return "BackSpace";
        case SDL_SCANCODE_LSHIFT:  return "Shift_L";
        case SDL_SCANCODE_RSHIFT:  return "Shift_R";
        case SDL_SCANCODE_LCTRL:   return "Control_L";
        case SDL_SCANCODE_RCTRL:   return "Control_R";
        case SDL_SCANCODE_CAPSLOCK:return "Caps_Lock";
        case SDL_SCANCODE_NUMLOCKCLEAR: return "Num_Lock";
        case SDL_SCANCODE_LALT:     return "Alt_L";
        case SDL_SCANCODE_RALT:     return "Alt_R";
        default: break;
    }
    // Function keys F1-F12.
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) {
        const int n = 1 + (sc - SDL_SCANCODE_F1);
        return "F" + std::to_string(n);
    }
    // Letters a-z, digits 0-9: use the SDL name lowercased to match X keysyms.
    const char* name = SDL_GetScancodeName(sc);
    if (name && name[0]) return std::string{name};
    return {};
}

struct Gui::Impl {
    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    // The mutable working copy of the config; published on change.
    Config cfg;
    bool cfg_loaded = false;
    // Keybind capture state.
    bool capturing_key = false;
    // Track whether any control changed this frame → publish.
    bool dirty = false;
};

Gui::Gui() : impl_(std::make_unique<Impl>()) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        throw FatalError{std::string{"gui: SDL_Init failed: "} + SDL_GetError()};

    impl_->window = SDL_CreateWindow("pixelbot",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        420, 600, SDL_WINDOW_SHOWN);
    if (!impl_->window)
        throw FatalError{std::string{"gui: SDL_CreateWindow failed: "} + SDL_GetError()};

    // SDL_RENDERER_SOFTWARE forces the software path (no GL/EGL needed under
    // Xwayland). If it fails, fall back to the default (may use GL/accelerated).
    impl_->renderer = SDL_CreateRenderer(impl_->window, -1, SDL_RENDERER_SOFTWARE);
    if (!impl_->renderer)
        impl_->renderer = SDL_CreateRenderer(impl_->window, -1, SDL_RENDERER_ACCELERATED);
    if (!impl_->renderer)
        impl_->renderer = SDL_CreateRenderer(impl_->window, -1, 0);
    if (!impl_->renderer)
        throw FatalError{std::string{"gui: SDL_CreateRenderer failed: "} + SDL_GetError()};

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForSDLRenderer(impl_->window, impl_->renderer);
    ImGui_ImplSDLRenderer2_Init(impl_->renderer);
    ImGui::StyleColorsDark();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load the current config snapshot as the working copy.
    auto snap = ConfigStore::instance().snapshot();
    if (snap) { impl_->cfg = clone_config(*snap); impl_->cfg_loaded = true; }
}

Gui::~Gui() {
    if (impl_) {
        if (impl_->renderer) {
            ImGui_ImplSDLRenderer2_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
            SDL_DestroyRenderer(impl_->renderer);
        }
        if (impl_->window) SDL_DestroyWindow(impl_->window);
        SDL_Quit();
    }
}

void Gui::publish() {
    // Deep-copy the working config into a shared_ptr<const Config> and publish.
    auto c = std::make_shared<Config>(clone_config(impl_->cfg));
    ConfigStore::instance().publish(std::move(c));
}

void Gui::render() {
    if (!impl_->cfg_loaded) {
        ImGui::TextUnformatted("no config loaded");
        return;
    }
    auto it = impl_->cfg.profiles.find(impl_->cfg.active_profile);
    Profile* prof = (it != impl_->cfg.profiles.end()) ? &it->second : nullptr;
    if (!prof) {
        ImGui::TextUnformatted("active profile not found");
        return;
    }
    Profile& p = *prof;
    auto& d = p.detection;
    auto& a = p.aim;
    auto& k = p.keybind;

    if (ImGui::CollapsingHeader("Detection", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* techs[] = {"color", "edge", "composite"};
        int ti = static_cast<int>(d.technique);
        ImGui::Combo("Technique", &ti, techs, 3);
        if (ti != static_cast<int>(d.technique)) { d.technique = static_cast<DetectionConfig::Technique>(ti); impl_->dirty = true; }

        if (ImGui::SliderInt("FOV radius", &d.fov_radius, 10, 1000)) impl_->dirty = true;
        if (ImGui::SliderInt("Min area", &d.min_area, 1, 500)) impl_->dirty = true;
        if (ImGui::SliderInt("Head offset", &d.head_offset, 0, 30)) impl_->dirty = true;
        if (ImGui::SliderFloat("Hysteresis dist", &d.hysteresis_distance, 0.0f, 200.0f)) impl_->dirty = true;
        if (ImGui::SliderFloat("Hysteresis factor", &d.hysteresis_factor, 0.0f, 2.0f)) impl_->dirty = true;
        if (ImGui::SliderInt("Edge threshold", &d.edge_threshold, 0, 255)) impl_->dirty = true;
        if (ImGui::SliderFloat("Edge min aspect", &d.edge_min_aspect, 0.0f, 5.0f)) impl_->dirty = true;
    }

    if (ImGui::CollapsingHeader("Aim", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderFloat("X sensitivity", &a.x_sensitivity, 0.0f, 2.0f)) impl_->dirty = true;
        if (ImGui::SliderFloat("Y sensitivity", &a.y_sensitivity, 0.0f, 2.0f)) impl_->dirty = true;
        if (ImGui::SliderFloat("Smoothing", &a.smoothing, 0.0f, 1.0f)) impl_->dirty = true;
    }

    if (ImGui::CollapsingHeader("Keybind", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* modes[] = {"hold", "toggle"};
        int mi = (k.aim_mode == "toggle") ? 1 : 0;
        ImGui::Combo("Aim mode", &mi, modes, 2);
        if ((mi == 1) != (k.aim_mode == "toggle")) { k.aim_mode = (mi == 1) ? "toggle" : "hold"; impl_->dirty = true; }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Aim key: %s", k.aim_key.empty() ? "(always on)" : k.aim_key.c_str());
        ImGui::SameLine();
        if (impl_->capturing_key) {
            if (ImGui::Button("Press a key/button (Esc to cancel)##capture")) {}
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) impl_->capturing_key = false;
        } else {
            if (ImGui::Button("Set##setkey")) impl_->capturing_key = true;
            ImGui::SameLine();
            if (ImGui::Button("Clear")) { k.aim_key.clear(); impl_->dirty = true; }
        }
        ImGui::TextDisabled("Keys: F1-F12, space, Shift, Ctrl, Alt, Caps_Lock.\nButtons: Mouse1..Mouse5 (set via config).");
    }

    if (ImGui::CollapsingHeader("Capture")) {
        if (ImGui::SliderInt("region_x", &p.capture.region_x, 0, 3840)) impl_->dirty = true;
        if (ImGui::SliderInt("region_y", &p.capture.region_y, 0, 2160)) impl_->dirty = true;
        if (ImGui::SliderInt("region_w", &p.capture.region_w, 64, 3840)) impl_->dirty = true;
        if (ImGui::SliderInt("region_h", &p.capture.region_h, 64, 2160)) impl_->dirty = true;
    }

    if (ImGui::Button("Reload from file")) {
        ConfigStore::instance().reload();
        auto snap = ConfigStore::instance().snapshot();
        if (snap) impl_->cfg = clone_config(*snap);
    }
}

bool Gui::tick() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT) return false;
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID == SDL_GetWindowID(impl_->window))
            return false;

        // Keybind capture: grab the next key/button press.
        if (impl_->capturing_key) {
            if (event.type == SDL_KEYDOWN) {
                // Esc cancels; any other key sets the bind.
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    impl_->capturing_key = false;
                } else {
                    auto it = impl_->cfg.profiles.find(impl_->cfg.active_profile);
                    Profile* prof = (it != impl_->cfg.profiles.end()) ? &it->second : nullptr;
                    if (prof) {
                        std::string name = scancode_to_keysym_name(event.key.keysym.scancode);
                        if (!name.empty()) {
                            prof->keybind.aim_key = std::move(name);
                            impl_->dirty = true;
                        }
                    }
                    impl_->capturing_key = false;
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                auto it = impl_->cfg.profiles.find(impl_->cfg.active_profile);
                Profile* prof = (it != impl_->cfg.profiles.end()) ? &it->second : nullptr;
                if (prof) {
                    prof->keybind.aim_key = "Mouse" + std::to_string(event.button.button);
                    impl_->dirty = true;
                }
                impl_->capturing_key = false;
            }
        }
    }

    ImGui_ImplSDL2_NewFrame();
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(400, 580), ImGuiCond_FirstUseEver);
    ImGui::Begin("pixelbot", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    render();
    ImGui::End();

    ImGui::Render();
    SDL_SetRenderDrawColor(impl_->renderer, 30, 30, 30, 255);
    SDL_RenderClear(impl_->renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), impl_->renderer);
    SDL_RenderPresent(impl_->renderer);

    if (impl_->dirty) { publish(); impl_->dirty = false; }
    return true;
}

} // namespace pixelbot
