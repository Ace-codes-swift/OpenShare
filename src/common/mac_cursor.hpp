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
#include <vector>

namespace openshare {

/// Capture the current system cursor as BGRA pixels sized for a framebuffer
/// that uses `backingScale` (1.0 on non-Retina, 2.0 on typical Retina).
/// Hotspot is returned in the same pixel space as the bitmap.
bool captureSystemCursor(double backingScale,
                         std::vector<uint8_t>& bgra,
                         int& width,
                         int& height,
                         int& hotX,
                         int& hotY);

} // namespace openshare
