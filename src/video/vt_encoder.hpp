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
#include <functional>
#include <vector>

namespace openshare {

// Encodes BGRA frames to H.264 Annex-B (start-code prefixed NAL units).
class VtEncoder {
public:
    using OutputCallback = std::function<void(const uint8_t* data, size_t len, bool keyframe)>;

    struct Impl;

    VtEncoder() = default;
    ~VtEncoder();

    VtEncoder(const VtEncoder&) = delete;
    VtEncoder& operator=(const VtEncoder&) = delete;

    bool start(int width, int height, int fps, OutputCallback cb);
    void stop();

    // BGRA, rowBytes typically width*4
    bool encodeFrame(const uint8_t* bgra, size_t rowBytes, int64_t ptsMs);

    void forceKeyframe();

private:
    Impl* impl_ = nullptr;
};

} // namespace openshare
