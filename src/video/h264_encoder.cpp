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

#include "h264_encoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <cstring>
#include <mutex>
#include <vector>

namespace openshare {

struct H264Encoder::Impl {
    AVCodecContext* codec = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* sws = nullptr;
    OutputCallback callback;
    int width = 0;
    int height = 0;
    bool forceKey = true;
    std::mutex mutex;
    std::vector<uint8_t> annexB;
    std::vector<uint8_t> paramSets;
};

static void appendAnnexBNal(std::vector<uint8_t>& out, const uint8_t* data, size_t size) {
    out.push_back(0);
    out.push_back(0);
    out.push_back(0);
    out.push_back(1);
    out.insert(out.end(), data, data + size);
}

static bool looksLikeAnnexB(const uint8_t* data, size_t len) {
    return len >= 3 && data[0] == 0 && data[1] == 0 &&
           (data[2] == 1 || (len >= 4 && data[2] == 0 && data[3] == 1));
}

static void appendAvccToAnnexB(std::vector<uint8_t>& out, const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset + 4 <= size) {
        const uint32_t nalLen = (uint32_t(data[offset]) << 24) | (uint32_t(data[offset + 1]) << 16) |
                                (uint32_t(data[offset + 2]) << 8) | uint32_t(data[offset + 3]);
        offset += 4;
        if (nalLen == 0 || offset + nalLen > size) {
            break;
        }
        appendAnnexBNal(out, data + offset, nalLen);
        offset += nalLen;
    }
}

static void extractParamSetsFromExtradata(const uint8_t* extradata, int extradataSize,
                                          std::vector<uint8_t>& out) {
    if (!extradata || extradataSize < 7) {
        return;
    }

    int pos = 5;
    const int numSps = extradata[pos++] & 0x1f;
    for (int i = 0; i < numSps && pos + 2 <= extradataSize; ++i) {
        const int len = (int(extradata[pos]) << 8) | int(extradata[pos + 1]);
        pos += 2;
        if (len <= 0 || pos + len > extradataSize) {
            return;
        }
        appendAnnexBNal(out, extradata + pos, static_cast<size_t>(len));
        pos += len;
    }

    if (pos >= extradataSize) {
        return;
    }

    const int numPps = extradata[pos++];
    for (int i = 0; i < numPps && pos + 2 <= extradataSize; ++i) {
        const int len = (int(extradata[pos]) << 8) | int(extradata[pos + 1]);
        pos += 2;
        if (len <= 0 || pos + len > extradataSize) {
            return;
        }
        appendAnnexBNal(out, extradata + pos, static_cast<size_t>(len));
        pos += len;
    }
}

static void deliverPacket(H264Encoder::Impl* impl, AVPacket* packet) {
    if (!impl || !impl->callback || !packet || packet->size <= 0) {
        return;
    }

    const bool keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;

    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->annexB.clear();

    if (keyframe && !impl->paramSets.empty()) {
        impl->annexB.insert(impl->annexB.end(), impl->paramSets.begin(), impl->paramSets.end());
    }

    if (looksLikeAnnexB(packet->data, static_cast<size_t>(packet->size))) {
        impl->annexB.insert(impl->annexB.end(), packet->data, packet->data + packet->size);
    } else {
        appendAvccToAnnexB(impl->annexB, packet->data, static_cast<size_t>(packet->size));
    }

    if (!impl->annexB.empty()) {
        impl->callback(impl->annexB.data(), impl->annexB.size(), keyframe);
    }
}

static void drainPackets(H264Encoder::Impl* impl) {
    while (impl && impl->codec && impl->packet) {
        const int ret = avcodec_receive_packet(impl->codec, impl->packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            break;
        }
        deliverPacket(impl, impl->packet);
        av_packet_unref(impl->packet);
    }
}

H264Encoder::~H264Encoder() {
    stop();
}

bool H264Encoder::start(int width, int height, int fps, OutputCallback cb) {
    stop();

    if (width <= 0 || height <= 0 || !cb) {
        return false;
    }

    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        return false;
    }

    auto* impl = new Impl();
    impl_ = impl;
    impl->callback = std::move(cb);
    impl->width = width;
    impl->height = height;
    impl->forceKey = true;

    const int effectiveFps = fps > 0 ? fps : 30;

    impl->codec = avcodec_alloc_context3(codec);
    if (!impl->codec) {
        stop();
        return false;
    }

    impl->codec->width = width;
    impl->codec->height = height;
    impl->codec->pix_fmt = AV_PIX_FMT_YUV420P;
    impl->codec->time_base = AVRational{1, 1000};
    impl->codec->framerate = AVRational{effectiveFps, 1};
    impl->codec->gop_size = effectiveFps * 2;
    impl->codec->max_b_frames = 0;

    int64_t target = static_cast<int64_t>(width) * height * effectiveFps / 10;
    if (target < 4'000'000) {
        target = 4'000'000;
    }
    if (target > 30'000'000) {
        target = 30'000'000;
    }
    impl->codec->bit_rate = target;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "preset", "veryfast", 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);
    av_dict_set(&opts, "profile", "main", 0);
    av_dict_set(&opts, "x264-params", "annexb=1", 0);

    if (avcodec_open2(impl->codec, codec, &opts) < 0) {
        av_dict_free(&opts);
        stop();
        return false;
    }
    av_dict_free(&opts);

    impl->paramSets.clear();
    extractParamSetsFromExtradata(impl->codec->extradata, impl->codec->extradata_size, impl->paramSets);

    impl->frame = av_frame_alloc();
    impl->packet = av_packet_alloc();
    if (!impl->frame || !impl->packet) {
        stop();
        return false;
    }

    impl->frame->format = impl->codec->pix_fmt;
    impl->frame->width = width;
    impl->frame->height = height;
    if (av_frame_get_buffer(impl->frame, 32) < 0) {
        stop();
        return false;
    }

    impl->sws = sws_getContext(width,
                               height,
                               AV_PIX_FMT_BGRA,
                               width,
                               height,
                               AV_PIX_FMT_YUV420P,
                               SWS_BILINEAR,
                               nullptr,
                               nullptr,
                               nullptr);
    if (!impl->sws) {
        stop();
        return false;
    }

    return true;
}

void H264Encoder::stop() {
    if (!impl_) {
        return;
    }

    if (impl_->codec) {
        avcodec_send_frame(impl_->codec, nullptr);
        drainPackets(impl_);
        avcodec_free_context(&impl_->codec);
    }

    if (impl_->frame) {
        av_frame_free(&impl_->frame);
    }
    if (impl_->packet) {
        av_packet_free(&impl_->packet);
    }
    if (impl_->sws) {
        sws_freeContext(impl_->sws);
        impl_->sws = nullptr;
    }

    delete impl_;
    impl_ = nullptr;
}

void H264Encoder::forceKeyframe() {
    if (impl_) {
        impl_->forceKey = true;
    }
}

bool H264Encoder::encodeFrame(const uint8_t* bgra, size_t rowBytes, int64_t ptsMs) {
    if (!impl_ || !impl_->codec || !impl_->frame || !impl_->sws || !bgra) {
        return false;
    }

    const uint8_t* srcSlice[1] = {bgra};
    int srcStride[1] = {static_cast<int>(rowBytes)};
    sws_scale(impl_->sws, srcSlice, srcStride, 0, impl_->height, impl_->frame->data, impl_->frame->linesize);

    impl_->frame->pts = ptsMs;
    if (impl_->forceKey) {
        impl_->frame->pict_type = AV_PICTURE_TYPE_I;
        impl_->frame->flags |= AV_FRAME_FLAG_KEY;
        impl_->forceKey = false;
    } else {
        impl_->frame->pict_type = AV_PICTURE_TYPE_NONE;
        impl_->frame->flags &= ~AV_FRAME_FLAG_KEY;
    }

    if (avcodec_send_frame(impl_->codec, impl_->frame) < 0) {
        return false;
    }

    if (impl_->paramSets.empty() && impl_->codec->extradata && impl_->codec->extradata_size > 0) {
        extractParamSetsFromExtradata(impl_->codec->extradata, impl_->codec->extradata_size,
                                    impl_->paramSets);
    }

    drainPackets(impl_);
    return true;
}

} // namespace openshare
