// src/vision/composite_detector.hpp
#pragma once
#include "core/detector.hpp"
namespace pixelbot {
class CompositeDetector : public Detector {
public:
    Target detect(const Frame& frame, const DetectionConfig& cfg) override;
private:
    Target last_;
};
} // namespace pixelbot
