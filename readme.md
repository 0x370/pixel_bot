# pixelbot

A C++23 color/edge aim-assist bot for FPS games on X11 Linux. It captures the
screen, detects a target silhouette by color and/or edge shape, and moves the
mouse toward it through a virtual uinput pointer — all in a low-latency
two-thread pipeline.

Built greenfield with no external vision or game libraries: capture is Xlib +
MIT-SHM, detection is hand-rolled HSV thresholding, Sobel edges, and connected
components, and the GUI is Dear ImGui on SDL2.

## Features

- **Three detection techniques** (`color`, `edge`, `composite`), selectable per
  profile:
  - *Color* — HSV thresholding over one or more color ranges.
  - *Edge* — Sobel magnitude thresholded, filtered by a tall-only aspect ratio.
  - *Composite* (default) — color mask AND edge mask fused, then connected
    components; a pixel must match the target color *and* sit on a strong edge,
    which rejects most HUD elements.
- **Aim assist** — target selection inside a configurable FOV radius, with
  hysteresis against flicker, head offset, per-axis sensitivity, and EMA
  smoothing.
- **Keybinds** — aim gated on a key/button (`F1`, `space`, `Caps_Lock`,
  `Mouse1`–`Mouse5`) in either `hold` or `toggle` mode, or always enabled.
- **Live config editing** — Dear ImGui control panel with sliders/toggles and a
  keybind capture button; every change is published atomically to the running
  pipeline. Config also reloads on `SIGHUP`.
- **Window auto-detection** — dropdown of open X11 windows and direct window
  capture (handles Vulkan/Xwayland where a fixed region would miss).
- **Safe by default** — `--dry-run` logs target deltas without touching the
  mouse; `--once` captures a single frame and prints its geometry.
- **Self-test** — in-process synthetic-frame tests for the vision pipeline, no
  X11 or game needed.

## How it works

```mermaid
flowchart LR
    subgraph Capture thread
        A[X11 SHM capture] --> B[copy into FrameSnap]
    end
    B -->|atomic shared_ptr, latest wins| C[aim thread]
    subgraph Aim thread ~1 kHz
        C --> D[detect target]
        D --> E[center-relative delta]
        E --> F[EMA smoothing]
        F --> G{aim enabled?}
        G -->|yes| H[uinput mouse move]
        G -->|no| C
    end
    I[KeybindPoller] --> G
    J[ConfigStore snapshot] --> D
    J --> F
    J --> G
```

Two threads exchange one `shared_ptr<FrameSnap>` atomically: the capture
thread always overwrites (latest-wins, frames drop rather than queue), the aim
thread reads a snapshot and runs detect → translate → smooth → move, paced at
~1 kHz. Config is a `shared_ptr<const Config>` snapshot the aim loop reloads
each iteration, so GUI edits and `SIGHUP` reloads take effect without stopping
the pipeline.

## Requirements

- Linux with X11 (Xlib + Xext), C++23 compiler:
  - **GCC ≥ 14** with `libstdc++exp`, or **GCC 12–13** with
    `libstdc++_libbacktrace` (for `std::stacktrace`; CMake fails with a clear
    error if neither is found).
- **CMake ≥ 3.28**, pkg-config.
- System packages: `sdl2`, `libx11`, `libxext` (dev headers).
- Fetched at build time (FetchContent): `nlohmann/json` v3.11.3 and
  Dear ImGui v1.92.8.
- Mouse injection requires write access to `/dev/uinput` (root or the `input`
  group). `--dry-run` skips it entirely.

## Build

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build        # run the vision self-test
```

Binaries: `build/pixelbot` and `build/pixelbot-selftest`.

## Usage

```
pixelbot [--config <path>] [--dry-run] [--once] [--no-gui]
```

| Flag | Effect |
|---|---|
| `--config <path>` | Config file (default `./config.json`). |
| `--dry-run` | No mouse input; log target + delta to stderr each frame. |
| `--once` | Capture one frame, print `w h stride` plus the center pixel in BGRA, exit. |
| `--no-gui` | Headless: run without the control panel, block until signal. |

Default mode opens the ImGui control panel in its own SDL2 window; closing it
exits. Signals: `SIGINT`/`SIGTERM` shut down gracefully, `SIGHUP` reloads the
config file.

## Configuration

`config.json` selects the active profile and the backends:

```jsonc
{
  "active_profile": "csgo",
  "capture_backend": "x11",      // capture source
  "input_backend": "uinput",     // mouse injection (ignored with --dry-run)
  "profiles": {
    "csgo": {
      "capture":   { "region_x": 0, "region_y": 0, "region_w": 1920, "region_h": 1080 },
      "detection": {
        "technique": "composite",           // color | edge | composite
        "color_ranges": [ { "h": [0, 10], "s": [100, 255], "v": [100, 255] } ],
        "fov_radius": 250,                  // search radius around screen center
        "min_area": 12,                     // min blob size in px
        "head_offset": 3,                   // aim point above the blob top
        "hysteresis_distance": 30,          // stickiness against flicker
        "hysteresis_factor": 0.8,
        "edge_threshold": 90,
        "edge_min_aspect": 0.8              // tall-only filter
      },
      "aim":      { "x_sensitivity": 0.18, "y_sensitivity": 0.18, "smoothing": 0.25 },
      "keybind":  { "aim_key": "Mouse5", "aim_mode": "hold" }   // "" = always on
    }
  }
}
```

The schema is fixed and strictly validated — missing or out-of-range keys are
fatal errors, not silently ignored.

## Project layout

```
src/
  main.cpp              entry point, CLI parsing, backend wiring
  config.{hpp,cpp}      JSON schema, atomic snapshot store, SIGHUP reload
  pipeline.{hpp,cpp}    capture + aim threads, latest-frame handoff
  error.hpp             FatalError + std::stacktrace, crash/signal handlers
  core/                 abstract interfaces: CaptureBackend, Detector, MouseInput, Frame
  capture/              X11 SHM capture, window finder
  vision/               color/edge/composite detectors, primitives (HSV, Sobel, CCL)
  aim/                  EMA-smoothed delta computation
  input/                uinput mouse, X11 keybind poller
  gui/                  Dear ImGui control panel
tests/
  self_test.cpp         synthetic-frame vision tests (no X11/uinput needed)
```

## Testing

`ctest` runs `pixelbot-selftest`, which synthesizes BGRA frames in-process and
asserts each detector: color finds an off-center red rectangle, edge rejects
wide (HUD-like) shapes, composite finds the tall red silhouette while rejecting
a red HUD bar. No display or game required. `pixelbot --once` doubles as a
capture smoke test.

## Disclaimer

This is an aim-assist tool for games. Using it online may violate a game's
terms of service and can get accounts banned; it is intended for offline
testing, modding, and educational use. There is deliberately no click/
triggerbot functionality.
