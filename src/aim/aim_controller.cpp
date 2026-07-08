// src/aim/aim_controller.cpp
#include "aim_controller.hpp"
#include <cmath>

namespace pixelbot {

Delta AimController::compute(const Target& target, const AimConfig& cfg) {
    if (!target.valid) {
        prev_dx_ = 0.0f;
        prev_dy_ = 0.0f;
        return {0.0f, 0.0f};
    }
    // target.x/y arrive as offset-from-screen-center (the pipeline subtracts
    // frame_center before calling). raw = offset * sensitivity, then EMA.
    const float raw_dx = target.x * cfg.x_sensitivity;
    const float raw_dy = target.y * cfg.y_sensitivity;
    // EMA: smoothed = alpha*raw + (1-alpha)*prev. alpha = smoothing.
    const float a = cfg.smoothing;
    prev_dx_ = a * raw_dx + (1.0f - a) * prev_dx_;
    prev_dy_ = a * raw_dy + (1.0f - a) * prev_dy_;
    return {prev_dx_, prev_dy_};
}

} // namespace pixelbot
