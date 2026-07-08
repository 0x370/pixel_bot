// src/gui/gui.hpp — Dear ImGui control panel for live config editing.
//
// A standalone SDL2 window (not an overlay). Sliders/toggles edit the active
// profile's detection + aim + keybind settings; the keybind editor captures
// the next key/button press as the aim key. On any change the GUI publishes a
// new Config snapshot to ConfigStore, which the aim loop picks up atomically.
// The GUI runs on the main thread; the pipeline's capture/aim threads run in
// the background.
#pragma once
#include "config.hpp"
#include "input/keybind_poller.hpp"
#include <atomic>
#include <memory>

namespace pixelbot {

class Gui {
public:
    Gui();
    ~Gui();
    Gui(const Gui&) = delete;
    Gui& operator=(const Gui&) = delete;

    // One iteration of the SDL event pump + ImGui render. Returns false when
    // the user closed the window (quit requested). Call in a loop from main.
    [[nodiscard]] bool tick();

private:
    void render();
    void publish();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pixelbot
