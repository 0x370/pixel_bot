// src/input/uinput_mouse.hpp — virtual mouse via /dev/uinput.
#pragma once
#include "core/mouse.hpp"

namespace pixelbot {

class UinputMouse : public MouseInput {
public:
    UinputMouse();
    ~UinputMouse() override;
    UinputMouse(const UinputMouse&) = delete;
    UinputMouse& operator=(const UinputMouse&) = delete;
    void move(int dx, int dy) override;

private:
    int fd_ = -1;
};

} // namespace pixelbot
