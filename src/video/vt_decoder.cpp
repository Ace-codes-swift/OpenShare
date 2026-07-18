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

#include "vt_decoder.hpp"

#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

#include <cstring>
#include <mutex>
#include <vector>

namespace openshare {

struct VtDecoder::Impl {
    VTDecompressionSessionRef session = nullptr;
    CMVideoFormatDescriptionRef format = nullptr;
    FrameCallback callback;
    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;
    std::mutex mutex;
    std::vector<uint8_t> bgraScratch;
    int width = 0;
    int height = 0;
};

static std::vector<std::pair<const uint8_t*, size_t>> splitAnnexB(const uint8_t* data, size_t len) {
    std::vector<std::pair<const uint8_t*, size_t>> nals;
    size_t i = 0;
    while (i + 3 < len) {
        size_t startCode = 0;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            startCode = 3;
        } else if (i + 4 <= len && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 &&
                   data[i + 3] == 1) {
            startCode = 4;
        } else {
            ++i;
            continue;
        }
        const size_t nalStart = i + startCode;
        size_t next = nalStart;
        while (next + 3 < len) {
            if (data[next] == 0 && data[next + 1] == 0 &&
                (data[next + 2] == 1 ||
                 (next + 4 <= len && data[next + 2] == 0 && data[next + 3] == 1))) {
                break;
            }
            ++next;
        }
        if (next + 3 >= len) {
            next = len;
        }
        if (nalStart < next) {
            nals.emplace_back(data + nalStart, next - nalStart);
        }
        i = next;
    }
    return nals;
}

static void decompressionCallback(void* decompressionOutputRefCon,
                                  void* /*sourceFrameRefCon*/,
                                  OSStatus status,
                                  VTDecodeInfoFlags /*infoFlags*/,
                                  CVImageBufferRef imageBuffer,
                                  CMTime /*presentationTimeStamp*/,
                                  CMTime /*presentationDuration*/) {
    auto* impl = static_cast<VtDecoder::Impl*>(decompressionOutputRefCon);
    if (!impl || status != noErr || !imageBuffer || !impl->callback) {
        return;
    }

    CVPixelBufferRef pixelBuffer = static_cast<CVPixelBufferRef>(imageBuffer);
    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

    const int width = static_cast<int>(CVPixelBufferGetWidth(pixelBuffer));
    const int height = static_cast<int>(CVPixelBufferGetHeight(pixelBuffer));
    const size_t stride = CVPixelBufferGetBytesPerRow(pixelBuffer);
    const uint8_t* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pixelBuffer));

    // Copy into contiguous BGRA for the callback (SDL needs stable buffer).
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->width = width;
        impl->height = height;
        impl->bgraScratch.resize(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y) {
            std::memcpy(impl->bgraScratch.data() + y * width * 4, base + y * stride, width * 4);
        }
        impl->callback(impl->bgraScratch.data(), width, height, width * 4);
    }

    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
}

static bool createSession(VtDecoder::Impl* impl) {
    if (impl->sps.empty() || impl->pps.empty()) {
        return false;
    }

    const uint8_t* paramSetPointers[2] = {impl->sps.data(), impl->pps.data()};
    size_t paramSetSizes[2] = {impl->sps.size(), impl->pps.size()};

    if (impl->format) {
        CFRelease(impl->format);
        impl->format = nullptr;
    }

    OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
        kCFAllocatorDefault, 2, paramSetPointers, paramSetSizes, 4, &impl->format);
    if (status != noErr || !impl->format) {
        return false;
    }

    if (impl->session) {
        VTDecompressionSessionInvalidate(impl->session);
        CFRelease(impl->session);
        impl->session = nullptr;
    }

    CFMutableDictionaryRef destAttrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    OSType pixelFormat = kCVPixelFormatType_32BGRA;
    CFNumberRef pf = CFNumberCreate(nullptr, kCFNumberSInt32Type, &pixelFormat);
    CFDictionarySetValue(destAttrs, kCVPixelBufferPixelFormatTypeKey, pf);
    CFRelease(pf);

    VTDecompressionOutputCallbackRecord callback{};
    callback.decompressionOutputCallback = decompressionCallback;
    callback.decompressionOutputRefCon = impl;

    status = VTDecompressionSessionCreate(
        kCFAllocatorDefault, impl->format, nullptr, destAttrs, &callback, &impl->session);
    CFRelease(destAttrs);

    return status == noErr && impl->session != nullptr;
}

VtDecoder::~VtDecoder() {
    stop();
}

bool VtDecoder::start(FrameCallback cb) {
    stop();
    impl_ = new Impl();
    impl_->callback = std::move(cb);
    return true;
}

void VtDecoder::stop() {
    if (!impl_) {
        return;
    }
    if (impl_->session) {
        VTDecompressionSessionWaitForAsynchronousFrames(impl_->session);
        VTDecompressionSessionInvalidate(impl_->session);
        CFRelease(impl_->session);
        impl_->session = nullptr;
    }
    if (impl_->format) {
        CFRelease(impl_->format);
        impl_->format = nullptr;
    }
    delete impl_;
    impl_ = nullptr;
}

bool VtDecoder::decode(const uint8_t* data, size_t len) {
    if (!impl_ || !data || len == 0) {
        return false;
    }

    auto nals = splitAnnexB(data, len);
    bool sawVcl = false;
    std::vector<uint8_t> avcc;

    for (const auto& [nal, nalLen] : nals) {
        if (nalLen == 0) {
            continue;
        }
        const uint8_t nalType = nal[0] & 0x1f;
        if (nalType == 7) { // SPS
            impl_->sps.assign(nal, nal + nalLen);
            if (impl_->session) {
                VTDecompressionSessionInvalidate(impl_->session);
                CFRelease(impl_->session);
                impl_->session = nullptr;
            }
        } else if (nalType == 8) { // PPS
            impl_->pps.assign(nal, nal + nalLen);
            if (impl_->session) {
                VTDecompressionSessionInvalidate(impl_->session);
                CFRelease(impl_->session);
                impl_->session = nullptr;
            }
        } else if (nalType == 5 || nalType == 1) { // IDR / non-IDR slice
            if (!impl_->session) {
                if (!createSession(impl_)) {
                    return false;
                }
            }
            // AVCC: 4-byte length + NAL
            uint32_t l = static_cast<uint32_t>(nalLen);
            avcc.push_back(static_cast<uint8_t>((l >> 24) & 0xff));
            avcc.push_back(static_cast<uint8_t>((l >> 16) & 0xff));
            avcc.push_back(static_cast<uint8_t>((l >> 8) & 0xff));
            avcc.push_back(static_cast<uint8_t>(l & 0xff));
            avcc.insert(avcc.end(), nal, nal + nalLen);
            sawVcl = true;
        }
    }

    if (!sawVcl || avcc.empty() || !impl_->session || !impl_->format) {
        return true; // parameter sets only is OK
    }

    CMBlockBufferRef block = nullptr;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault,
                                                         avcc.data(),
                                                         avcc.size(),
                                                         kCFAllocatorNull,
                                                         nullptr,
                                                         0,
                                                         avcc.size(),
                                                         0,
                                                         &block);
    if (status != noErr || !block) {
        return false;
    }

    CMSampleBufferRef sample = nullptr;
    const size_t sampleSize = avcc.size();
    status = CMSampleBufferCreateReady(kCFAllocatorDefault,
                                       block,
                                       impl_->format,
                                       1,
                                       0,
                                       nullptr,
                                       1,
                                       &sampleSize,
                                       &sample);
    CFRelease(block);
    if (status != noErr || !sample) {
        return false;
    }

    VTDecodeFrameFlags flags = kVTDecodeFrame_EnableAsynchronousDecompression;
    status = VTDecompressionSessionDecodeFrame(impl_->session, sample, flags, nullptr, nullptr);
    CFRelease(sample);
    // Wait so the callback fills bgra before return (simpler for SDL thread model)
    VTDecompressionSessionWaitForAsynchronousFrames(impl_->session);
    return status == noErr;
}

} // namespace openshare
