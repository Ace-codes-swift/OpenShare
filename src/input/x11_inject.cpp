/*
 * OpenShare
 * Copyright (C) 2026 Ace Jones / ATech
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "x11_inject.hpp"
#include "protocol.hpp"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <cmath>
#include <cstdlib>

namespace openshare {

namespace {

using protocol::kModAlt;
using protocol::kModCmd;
using protocol::kModCtrl;
using protocol::kModShift;

float clamp01(float v) {
    if (v < 0.f) {
        return 0.f;
    }
    if (v > 1.f) {
        return 1.f;
    }
    return v;
}

// macOS virtual key codes (wire format) → X11 KeySyms for a typical US Mac layout.
KeySym macKeycodeToKeysym(uint16_t macKeycode) {
    switch (macKeycode) {
    case 0x00:
        return XK_a;
    case 0x01:
        return XK_s;
    case 0x02:
        return XK_d;
    case 0x03:
        return XK_f;
    case 0x04:
        return XK_h;
    case 0x05:
        return XK_g;
    case 0x06:
        return XK_z;
    case 0x07:
        return XK_x;
    case 0x08:
        return XK_c;
    case 0x09:
        return XK_v;
    case 0x0B:
        return XK_b;
    case 0x0C:
        return XK_q;
    case 0x0D:
        return XK_w;
    case 0x0E:
        return XK_e;
    case 0x0F:
        return XK_r;
    case 0x10:
        return XK_y;
    case 0x11:
        return XK_t;
    case 0x12:
        return XK_1;
    case 0x13:
        return XK_2;
    case 0x14:
        return XK_3;
    case 0x15:
        return XK_4;
    case 0x16:
        return XK_6;
    case 0x17:
        return XK_5;
    case 0x18:
        return XK_equal;
    case 0x19:
        return XK_9;
    case 0x1A:
        return XK_7;
    case 0x1B:
        return XK_minus;
    case 0x1C:
        return XK_8;
    case 0x1D:
        return XK_0;
    case 0x1E:
        return XK_bracketright;
    case 0x1F:
        return XK_o;
    case 0x20:
        return XK_u;
    case 0x21:
        return XK_bracketleft;
    case 0x22:
        return XK_i;
    case 0x23:
        return XK_p;
    case 0x24:
        return XK_Return;
    case 0x25:
        return XK_l;
    case 0x26:
        return XK_j;
    case 0x27:
        return XK_apostrophe;
    case 0x28:
        return XK_k;
    case 0x29:
        return XK_semicolon;
    case 0x2A:
        return XK_backslash;
    case 0x2B:
        return XK_comma;
    case 0x2C:
        return XK_slash;
    case 0x2D:
        return XK_n;
    case 0x2E:
        return XK_m;
    case 0x2F:
        return XK_period;
    case 0x30:
        return XK_Tab;
    case 0x31:
        return XK_space;
    case 0x32:
        return XK_grave;
    case 0x33:
        return XK_BackSpace;
    case 0x35:
        return XK_Escape;
    case 0x37:
        return XK_Super_L;
    case 0x38:
        return XK_Shift_L;
    case 0x39:
        return XK_Caps_Lock;
    case 0x3A:
        return XK_Alt_L;
    case 0x3B:
        return XK_Control_L;
    case 0x3C:
        return XK_Shift_R;
    case 0x3D:
        return XK_Alt_R;
    case 0x3E:
        return XK_Control_R;
    case 0x60:
        return XK_F5;
    case 0x61:
        return XK_F6;
    case 0x62:
        return XK_F7;
    case 0x63:
        return XK_F3;
    case 0x64:
        return XK_F8;
    case 0x65:
        return XK_F9;
    case 0x67:
        return XK_F11;
    case 0x6D:
        return XK_F10;
    case 0x6F:
        return XK_F12;
    case 0x75:
        return XK_Delete;
    case 0x76:
        return XK_F4;
    case 0x78:
        return XK_F2;
    case 0x7A:
        return XK_F1;
    case 0x7B:
        return XK_Left;
    case 0x7C:
        return XK_Right;
    case 0x7D:
        return XK_Down;
    case 0x7E:
        return XK_Up;
    default:
        return NoSymbol;
    }
}

unsigned int protocolModifiersToX(uint32_t modifiers) {
    unsigned int state = 0;
    if (modifiers & kModShift) {
        state |= ShiftMask;
    }
    if (modifiers & kModCtrl) {
        state |= ControlMask;
    }
    if (modifiers & kModAlt) {
        state |= Mod1Mask;
    }
    if (modifiers & kModCmd) {
        state |= Mod4Mask;
    }
    return state;
}

int wheelTicks(int32_t delta) {
    if (delta == 0) {
        return 0;
    }
    const int ticks = static_cast<int>(std::abs(delta) + 31) / 32;
    return delta > 0 ? ticks : -ticks;
}

void fakeWheelButton(Display* display, unsigned int button, int ticks) {
    const int count = std::abs(ticks);
    for (int i = 0; i < count; ++i) {
        XTestFakeButtonEvent(display, button, True, CurrentTime);
        XTestFakeButtonEvent(display, button, False, CurrentTime);
    }
}

} // namespace

X11Injector::~X11Injector() {
    if (display_) {
        XCloseDisplay(display_);
        display_ = nullptr;
    }
}

bool X11Injector::ready() {
    if (display_) {
        return true;
    }

    display_ = XOpenDisplay(nullptr);
    if (!display_) {
        return false;
    }

    screen_ = DefaultScreen(display_);

    int eventBase = 0;
    int errorBase = 0;
    int major = 0;
    int minor = 0;
    if (!XTestQueryExtension(display_, &eventBase, &errorBase, &major, &minor)) {
        XCloseDisplay(display_);
        display_ = nullptr;
        return false;
    }

    return true;
}

void X11Injector::mouseMove(float nx, float ny) {
    if (!display_) {
        return;
    }

    nx = clamp01(nx);
    ny = clamp01(ny);

    const int w = DisplayWidth(display_, screen_);
    const int h = DisplayHeight(display_, screen_);
    const int x = static_cast<int>(nx * static_cast<float>(w - 1));
    const int y = static_cast<int>(ny * static_cast<float>(h - 1));

    XTestFakeMotionEvent(display_, screen_, x, y, CurrentTime);
    XFlush(display_);
}

void X11Injector::mouseButton(uint8_t button, bool down, float nx, float ny) {
    if (!display_) {
        return;
    }

    mouseMove(nx, ny);

    unsigned int xButton = Button1;
    switch (button) {
    case 1:
        xButton = Button3;
        break;
    case 2:
        xButton = Button2;
        break;
    default:
        xButton = Button1;
        break;
    }

    XTestFakeButtonEvent(display_, xButton, down ? True : False, CurrentTime);
    XFlush(display_);
}

void X11Injector::mouseWheel(int32_t dx, int32_t dy) {
    if (!display_) {
        return;
    }

    const int yTicks = wheelTicks(dy);
    if (yTicks > 0) {
        fakeWheelButton(display_, 5, yTicks);
    } else if (yTicks < 0) {
        fakeWheelButton(display_, 4, yTicks);
    }

    const int xTicks = wheelTicks(dx);
    if (xTicks > 0) {
        fakeWheelButton(display_, 7, xTicks);
    } else if (xTicks < 0) {
        fakeWheelButton(display_, 6, xTicks);
    }

    XFlush(display_);
}

void X11Injector::key(uint16_t macKeycode, uint32_t modifiers, bool down) {
    if (!display_) {
        return;
    }

    const KeySym sym = macKeycodeToKeysym(macKeycode);
    if (sym == NoSymbol) {
        return;
    }

    const KeyCode keycode = XKeysymToKeycode(display_, sym);
    if (keycode == 0) {
        return;
    }

    const unsigned int state = protocolModifiersToX(modifiers);

    XEvent ev{};
    ev.xkey.type = down ? KeyPress : KeyRelease;
    ev.xkey.display = display_;
    ev.xkey.window = RootWindow(display_, screen_);
    ev.xkey.root = ev.xkey.window;
    ev.xkey.subwindow = None;
    ev.xkey.time = CurrentTime;
    ev.xkey.x = 1;
    ev.xkey.y = 1;
    ev.xkey.same_screen = True;
    ev.xkey.keycode = keycode;
    ev.xkey.state = state;
    XSendEvent(display_, ev.xkey.window, True, down ? KeyPressMask : KeyReleaseMask, &ev);
    XFlush(display_);
}

} // namespace openshare
