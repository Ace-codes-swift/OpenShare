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
