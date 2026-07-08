// src/vision/detector_factory.cpp — make_detector factory.
#include "core/detector.hpp"
#include "color_detector.hpp"
#include "edge_detector.hpp"
#include "config.hpp"
#include "composite_detector.hpp"
#include "error.hpp"

namespace pixelbot {

std::unique_ptr<Detector> make_detector(int technique) {
    switch (technique) {
        case DetectionConfig::COLOR:     return std::make_unique<ColorDetector>();
        case DetectionConfig::EDGE:       return std::make_unique<EdgeDetector>();
        case DetectionConfig::COMPOSITE: return std::make_unique<CompositeDetector>();
        default: throw FatalError{"make_detector: unknown technique " +
                                 std::to_string(technique)};
    }
}

} // namespace pixelbot
