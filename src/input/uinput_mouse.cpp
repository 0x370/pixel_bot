// src/input/uinput_mouse.cpp — virtual mouse via /dev/uinput.
//
// Opens /dev/uinput, registers a relative-pointer device, and writes three
// input_events (EV_REL/REL_X, EV_REL/REL_Y, EV_SYN/SYN_REPORT) in a single
// write() per move() for lower latency. Requires write access to /dev/uinput
// (root or the 'input' group). --dry-run skips this entirely.
#include "uinput_mouse.hpp"
#include "error.hpp"
#include <linux/uinput.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <array>

namespace pixelbot {

UinputMouse::UinputMouse() {
    fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd_ < 0)
        throw FatalError{"uinput: cannot open /dev/uinput "
                         "(need root or the 'input' group)"};

    // Register a relative-pointer device.
    if (ioctl(fd_, UI_SET_EVBIT, EV_REL) < 0)
        throw FatalError{"uinput: UI_SET_EVBIT EV_REL failed"};
    if (ioctl(fd_, UI_SET_RELBIT, REL_X) < 0)
        throw FatalError{"uinput: UI_SET_RELBIT REL_X failed"};
    if (ioctl(fd_, UI_SET_RELBIT, REL_Y) < 0)
        throw FatalError{"uinput: UI_SET_RELBIT REL_Y failed"};

    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1234;
    usetup.id.product = 0x5678;
    usetup.id.version = 1;
    std::strncpy(usetup.name, "pixelbot", sizeof(usetup.name));

    if (ioctl(fd_, UI_DEV_SETUP, &usetup) < 0)
        throw FatalError{"uinput: UI_DEV_SETUP failed"};
    if (ioctl(fd_, UI_DEV_CREATE) < 0)
        throw FatalError{"uinput: UI_DEV_CREATE failed"};

    // Give userspace time to register the new device node. The kernel doc
    // example sleeps 1s; 150ms is enough locally (raise if events drop).
    usleep(150 * 1000);
}

void UinputMouse::move(int dx, int dy) {
    std::array<input_event, 3> evs{};
    evs[0].type = EV_REL;  evs[0].code = REL_X;      evs[0].value = dx;
    evs[1].type = EV_REL;  evs[1].code = REL_Y;      evs[1].value = dy;
    evs[2].type = EV_SYN;  evs[2].code = SYN_REPORT; evs[2].value = 0;

    const ssize_t want = static_cast<ssize_t>(sizeof(evs));
    ssize_t n = ::write(fd_, evs.data(), want);
    if (n != want)
        throw FatalError{"uinput: write failed (wrote " +
                         std::to_string(n) + " of " +
                         std::to_string(want) + " bytes)"};
}

UinputMouse::~UinputMouse() {
    if (fd_ >= 0) {
        ioctl(fd_, UI_DEV_DESTROY);
        ::close(fd_);
    }
}

} // namespace pixelbot
