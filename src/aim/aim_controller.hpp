// src/aim/aim_controller.hpp — EMA-smoothed aim delta computation.
#pragma once
#include "core/detector.hpp"
#include "config.hpp"

namespace pixelbot {

struct Delta { float dx = 0.0f, dy = 0.0f; };

class AimController {
public:
    // Compute the integer mouse delta toward the target. raw_dx/dy scaled by
    // sensitivity, then EMA-smoothed (alpha = smoothing: higher = more
    // responsive, lower = smoother). If !target.valid → {0,0} and reset prev.
    Delta compute(const Target& target, const AimConfig& cfg);

private:
    float prev_dx_ = 0.0f, prev_dy_ = 0.0f;
};

} // namespace pixelbot
