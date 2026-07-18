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

namespace openshare {

class MacInputInjector {
public:
    // Normalized [0,1] coords mapped to display pixel space using screenW/H.
    void mouseMove(float nx, float ny, int screenW, int screenH);
    void mouseButton(uint8_t button, bool down, float nx, float ny, int screenW, int screenH);
    // Deltas are in pixels (viewer pre-scales wheel ticks).
    void mouseWheel(int32_t dx, int32_t dy);
    void key(uint16_t keycode, uint32_t modifiers, bool down);

    static bool ensureAccessibilityPrompt();

private:
    // Which button is currently held, so moves become drag events.
    // -1 = none, else CGMouseButton value.
    int heldButton_ = -1;
};

} // namespace openshare
