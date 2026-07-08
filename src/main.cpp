// src/main.cpp — entry point: parse args, wire backends, run pipeline.
//
// Usage: pixelbot [--config <path>] [--dry-run] [--once]
//   --config <path>  config file (default ./config.json)
//   --dry-run        no mouse input; log target+delta to stderr each frame
//   --once           capture one frame, print "w h stride" + center BGRA, exit
//
// Errors are always fatal (FatalError → stacktrace + exit). SIGINT/SIGTERM
// release the shutdown semaphore for graceful join; SIGHUP reloads config.
#include "error.hpp"
#include "config.hpp"
#include "core/capture.hpp"
#include "core/detector.hpp"
#include "core/mouse.hpp"
#include "capture/x11_capture.hpp"
#include "input/uinput_mouse.hpp"
#include "pipeline.hpp"
#include "aim/aim_controller.hpp"
#include <print>
#include <span>
#include <semaphore>
#include <string>
#include <string_view>
#include <memory>

using namespace pixelbot;

// Build the capture backend from the config string.
static std::unique_ptr<CaptureBackend> make_capture(const Config& cfg) {
    if (cfg.capture_backend == "x11")
        return std::make_unique<X11Capture>(cfg.active().capture);
    throw FatalError{"unknown capture_backend: '" + cfg.capture_backend + "'"};
}

// Build the mouse input from the config string (nullptr in dry-run).
static std::unique_ptr<MouseInput> make_mouse(const Config& cfg, bool dry_run) {
    if (dry_run) return nullptr;
    if (cfg.input_backend == "uinput")
        return std::make_unique<UinputMouse>();
    throw FatalError{"unknown input_backend: '" + cfg.input_backend + "'"};
}

// --once: capture a single frame, print dimensions + center pixel, exit.
static int run_once(CaptureBackend& cap) {
    cap.start();
    Frame f = cap.capture();
    // Center pixel BGRA.
    const int cx = f.width / 2, cy = f.height / 2;
    const auto* px = reinterpret_cast<const unsigned char*>(f.data.data())
                     + static_cast<std::ptrdiff_t>(cy) * f.stride + cx * 4;
    std::println("{} {} {}  center_bgra={{{},{},{},{}}}",
                 f.width, f.height, f.stride, px[0], px[1], px[2], px[3]);
    cap.stop();
    return 0;
}

int main(int argc, char** argv) {
    install_crash_handlers();

    std::string config_path = "./config.json";
    bool dry_run = false;
    bool once = false;

    // Parse argv: --config <path>, --dry-run, --once.
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (a == "--dry-run") {
            dry_run = true;
        } else if (a == "--once") {
            once = true;
        } else {
            std::println(stderr, "pixelbot: unknown arg '{}'", a);
            std::println(stderr, "usage: pixelbot [--config <path>] [--dry-run] [--once]");
            return 1;
        }
    }

    // Load config + install SIGHUP for live reload.
    ConfigStore::instance().load(config_path);
    ConfigStore::install_sighup_handler();
    auto cfg = ConfigStore::instance().snapshot();

    auto capture = make_capture(*cfg);
    if (once) return run_once(*capture);

    auto detector = make_detector(cfg->active().detection.technique);
    auto mouse = make_mouse(*cfg, dry_run);

    Pipeline pl(std::move(capture), std::move(detector), std::move(mouse), dry_run);
    pl.start();

    // Block until SIGINT/SIGTERM releases the semaphore (graceful shutdown).
    std::binary_semaphore sem{0};
    set_shutdown_semaphore(&sem);
    sem.acquire();
    // Pipeline destructor joins the jthreads via stop tokens.
    return 0;
}
