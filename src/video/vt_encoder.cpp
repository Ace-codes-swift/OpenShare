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

#include "vt_encoder.hpp"

#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

#include <cstring>
#include <mutex>
#include <vector>

namespace openshare {

struct VtEncoder::Impl {
    VTCompressionSessionRef session = nullptr;
    OutputCallback callback;
    int width = 0;
    int height = 0;
    int64_t frameCount = 0;
    bool forceKey = true;
    std::mutex mutex;
    std::vector<uint8_t> annexB;
};

static void appendAnnexB(std::vector<uint8_t>& out, const uint8_t* data, size_t size) {
    // Convert AVCC length-prefixed NALs to Annex-B
    size_t offset = 0;
    while (offset + 4 <= size) {
        uint32_t nalLen = (uint32_t(data[offset]) << 24) | (uint32_t(data[offset + 1]) << 16) |
                          (uint32_t(data[offset + 2]) << 8) | uint32_t(data[offset + 3]);
        offset += 4;
        if (offset + nalLen > size) {
            break;
        }
        out.push_back(0);
        out.push_back(0);
        out.push_back(0);
        out.push_back(1);
        out.insert(out.end(), data + offset, data + offset + nalLen);
        offset += nalLen;
    }
}

static void extractParamSets(CMFormatDescriptionRef format, std::vector<uint8_t>& out) {
    size_t spsCount = 0;
    size_t ppsCount = 0;
    int nalLenSize = 0;
    const uint8_t* sps = nullptr;
    const uint8_t* pps = nullptr;
    size_t spsSize = 0;
    size_t ppsSize = 0;

    OSStatus status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
        format, 0, &sps, &spsSize, &spsCount, &nalLenSize);
    if (status == noErr && sps && spsSize > 0) {
        out.push_back(0);
        out.push_back(0);
        out.push_back(0);
        out.push_back(1);
        out.insert(out.end(), sps, sps + spsSize);
    }

    status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
        format, 1, &pps, &ppsSize, &ppsCount, &nalLenSize);
    if (status == noErr && pps && ppsSize > 0) {
        out.push_back(0);
        out.push_back(0);
        out.push_back(0);
        out.push_back(1);
        out.insert(out.end(), pps, pps + ppsSize);
    }
}

static void compressionCallback(void* outputCallbackRefCon,
                                void* /*sourceFrameRefCon*/,
                                OSStatus status,
                                VTEncodeInfoFlags /*infoFlags*/,
                                CMSampleBufferRef sampleBuffer) {
    auto* impl = static_cast<VtEncoder::Impl*>(outputCallbackRefCon);
    if (!impl || status != noErr || !sampleBuffer || !impl->callback) {
        return;
    }

    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->annexB.clear();

    CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sampleBuffer);

    bool isKeyframe = false;
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, true);
    if (attachments && CFArrayGetCount(attachments) > 0) {
        auto dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
        if (dict) {
            isKeyframe = !CFDictionaryContainsKey(dict, kCMSampleAttachmentKey_NotSync);
        }
    }

    if (isKeyframe && format) {
        extractParamSets(format, impl->annexB);
    }

    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!block) {
        return;
    }

    size_t totalLen = 0;
    char* dataPtr = nullptr;
    if (CMBlockBufferGetDataPointer(block, 0, nullptr, &totalLen, &dataPtr) != noErr || !dataPtr) {
        return;
    }

    appendAnnexB(impl->annexB, reinterpret_cast<const uint8_t*>(dataPtr), totalLen);

    if (!impl->annexB.empty()) {
        impl->callback(impl->annexB.data(), impl->annexB.size(), isKeyframe);
    }
}

VtEncoder::~VtEncoder() {
    stop();
}

bool VtEncoder::start(int width, int height, int fps, OutputCallback cb) {
    stop();
    impl_ = new Impl();
    impl_->callback = std::move(cb);
    impl_->width = width;
    impl_->height = height;

    CFMutableDictionaryRef encoderSpec = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    // Prefer hardware when available
    CFDictionarySetValue(encoderSpec,
                         kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder,
                         kCFBooleanTrue);

    CFMutableDictionaryRef sourceAttrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    int32_t w = width;
    int32_t h = height;
    OSType pixelFormat = kCVPixelFormatType_32BGRA;
    CFNumberRef widthNum = CFNumberCreate(nullptr, kCFNumberSInt32Type, &w);
    CFNumberRef heightNum = CFNumberCreate(nullptr, kCFNumberSInt32Type, &h);
    CFNumberRef pixelFormatNum = CFNumberCreate(nullptr, kCFNumberSInt32Type, &pixelFormat);
    CFDictionarySetValue(sourceAttrs, kCVPixelBufferWidthKey, widthNum);
    CFDictionarySetValue(sourceAttrs, kCVPixelBufferHeightKey, heightNum);
    CFDictionarySetValue(sourceAttrs, kCVPixelBufferPixelFormatTypeKey, pixelFormatNum);
    CFRelease(widthNum);
    CFRelease(heightNum);
    CFRelease(pixelFormatNum);

    OSStatus status = VTCompressionSessionCreate(kCFAllocatorDefault,
                                                 width,
                                                 height,
                                                 kCMVideoCodecType_H264,
                                                 encoderSpec,
                                                 sourceAttrs,
                                                 nullptr,
                                                 compressionCallback,
                                                 impl_,
                                                 &impl_->session);
    CFRelease(encoderSpec);
    CFRelease(sourceAttrs);

    if (status != noErr || !impl_->session) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
    VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_ProfileLevel,
                         kVTProfileLevel_H264_Main_AutoLevel);
    VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_AllowFrameReordering,
                         kCFBooleanFalse);

    // ~0.1 bits per pixel per frame: 1080p60 ≈ 12 Mbps, Retina 5K/2x ≈ 30 Mbps.
    // Clamped so native-resolution Retina streams don't saturate Wi-Fi.
    int64_t target = static_cast<int64_t>(width) * height * (fps > 0 ? fps : 30) / 10;
    if (target < 4'000'000) {
        target = 4'000'000;
    }
    if (target > 30'000'000) {
        target = 30'000'000;
    }
    const int32_t bitrate = static_cast<int32_t>(target);
    CFNumberRef bitrateNum = CFNumberCreate(nullptr, kCFNumberSInt32Type, &bitrate);
    VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_AverageBitRate, bitrateNum);
    CFRelease(bitrateNum);

    int32_t expectedFps = fps > 0 ? fps : 30;
    CFNumberRef fpsNum = CFNumberCreate(nullptr, kCFNumberSInt32Type, &expectedFps);
    VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_ExpectedFrameRate, fpsNum);
    CFRelease(fpsNum);

    int32_t keyInterval = expectedFps * 2;
    CFNumberRef keyNum = CFNumberCreate(nullptr, kCFNumberSInt32Type, &keyInterval);
    VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_MaxKeyFrameInterval, keyNum);
    CFRelease(keyNum);

    VTCompressionSessionPrepareToEncodeFrames(impl_->session);
    return true;
}

void VtEncoder::stop() {
    if (!impl_) {
        return;
    }
    if (impl_->session) {
        VTCompressionSessionCompleteFrames(impl_->session, kCMTimeInvalid);
        VTCompressionSessionInvalidate(impl_->session);
        CFRelease(impl_->session);
        impl_->session = nullptr;
    }
    delete impl_;
    impl_ = nullptr;
}

void VtEncoder::forceKeyframe() {
    if (impl_) {
        impl_->forceKey = true;
    }
}

bool VtEncoder::encodeFrame(const uint8_t* bgra, size_t rowBytes, int64_t ptsMs) {
    if (!impl_ || !impl_->session || !bgra) {
        return false;
    }

    CVPixelBufferRef pixelBuffer = nullptr;
    CVReturn cvStatus = CVPixelBufferCreate(kCFAllocatorDefault,
                                            impl_->width,
                                            impl_->height,
                                            kCVPixelFormatType_32BGRA,
                                            nullptr,
                                            &pixelBuffer);
    if (cvStatus != kCVReturnSuccess || !pixelBuffer) {
        return false;
    }

    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    uint8_t* dst = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixelBuffer));
    const size_t dstStride = CVPixelBufferGetBytesPerRow(pixelBuffer);
    for (int y = 0; y < impl_->height; ++y) {
        std::memcpy(dst + y * dstStride, bgra + y * rowBytes, impl_->width * 4);
    }
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);

    CMTime pts = CMTimeMake(ptsMs, 1000);
    CMTime duration = CMTimeMake(1, 30);

    CFDictionaryRef frameProps = nullptr;
    CFMutableDictionaryRef props = nullptr;
    if (impl_->forceKey) {
        props = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(props, kVTEncodeFrameOptionKey_ForceKeyFrame, kCFBooleanTrue);
        frameProps = props;
        impl_->forceKey = false;
    }

    OSStatus status = VTCompressionSessionEncodeFrame(
        impl_->session, pixelBuffer, pts, duration, frameProps, nullptr, nullptr);

    if (props) {
        CFRelease(props);
    }
    CFRelease(pixelBuffer);
    return status == noErr;
}

} // namespace openshare
