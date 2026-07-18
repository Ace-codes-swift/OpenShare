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

// Decodes H.264 Annex-B into BGRA frames.
class VtDecoder {
public:
    using FrameCallback = std::function<void(const uint8_t* bgra, int width, int height, size_t stride)>;

    struct Impl;

    VtDecoder() = default;
    ~VtDecoder();

    VtDecoder(const VtDecoder&) = delete;
    VtDecoder& operator=(const VtDecoder&) = delete;

    bool start(FrameCallback cb);
    void stop();

    // Feed Annex-B data (may contain multiple NALs / frames).
    bool decode(const uint8_t* data, size_t len);

private:
    Impl* impl_ = nullptr;
};

} // namespace openshare
