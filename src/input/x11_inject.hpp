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

#pragma once

#include <cstdint>

struct _XDisplay;
typedef struct _XDisplay Display;

namespace openshare {

class X11Injector {
public:
    X11Injector() = default;
    ~X11Injector();

    X11Injector(const X11Injector&) = delete;
    X11Injector& operator=(const X11Injector&) = delete;

    bool ready();

    // Normalized [0,1] coords mapped to DisplayWidth/Height.
    void mouseMove(float nx, float ny);
    void mouseButton(uint8_t button, bool down, float nx, float ny);
    // Deltas are in pixels; ~32 px per wheel tick maps to X buttons 4/5/6/7.
    void mouseWheel(int32_t dx, int32_t dy);
    void key(uint16_t macKeycode, uint32_t modifiers, bool down);

private:
    Display* display_ = nullptr;
    int screen_ = 0;
};

} // namespace openshare
