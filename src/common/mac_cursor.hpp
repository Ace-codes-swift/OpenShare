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
