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
