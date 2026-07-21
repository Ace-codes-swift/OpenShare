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

#include "h264_decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <cerrno>
#include <cstring>
#include <mutex>
#include <vector>

namespace openshare {

struct H264Decoder::Impl {
    AVCodecContext* codec = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* sws = nullptr;
    FrameCallback callback;
    std::mutex mutex;
    std::vector<uint8_t> bgraScratch;
    int width = 0;
    int height = 0;
    int swsSrcW = 0;
    int swsSrcH = 0;
};

static void emitFrame(H264Decoder::Impl* impl, AVFrame* frame) {
    if (!impl || !impl->callback || !frame) {
        return;
    }

    const int width = frame->width;
    const int height = frame->height;
    if (width <= 0 || height <= 0) {
        return;
    }

    if (!impl->sws || impl->swsSrcW != width || impl->swsSrcH != height) {
        if (impl->sws) {
            sws_freeContext(impl->sws);
            impl->sws = nullptr;
        }
        impl->sws = sws_getContext(width,
                                   height,
                                   static_cast<AVPixelFormat>(frame->format),
                                   width,
                                   height,
                                   AV_PIX_FMT_BGRA,
                                   SWS_BILINEAR,
                                   nullptr,
                                   nullptr,
                                   nullptr);
        impl->swsSrcW = width;
        impl->swsSrcH = height;
    }
    if (!impl->sws) {
        return;
    }

    const size_t stride = static_cast<size_t>(width) * 4;
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->width = width;
    impl->height = height;
    impl->bgraScratch.resize(stride * static_cast<size_t>(height));

    uint8_t* dstSlice[1] = {impl->bgraScratch.data()};
    int dstStride[1] = {static_cast<int>(stride)};
    sws_scale(impl->sws, frame->data, frame->linesize, 0, height, dstSlice, dstStride);
    impl->callback(impl->bgraScratch.data(), width, height, stride);
}

H264Decoder::~H264Decoder() {
    stop();
}

bool H264Decoder::start(FrameCallback cb) {
    stop();
    if (!cb) {
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        return false;
    }

    auto* impl = new Impl();
    impl_ = impl;
    impl->callback = std::move(cb);

    impl->codec = avcodec_alloc_context3(codec);
    if (!impl->codec) {
        stop();
        return false;
    }

    impl->codec->flags |= AV_CODEC_FLAG_LOW_DELAY;

    if (avcodec_open2(impl->codec, codec, nullptr) < 0) {
        stop();
        return false;
    }

    impl->frame = av_frame_alloc();
    impl->packet = av_packet_alloc();
    if (!impl->frame || !impl->packet) {
        stop();
        return false;
    }

    return true;
}

void H264Decoder::stop() {
    if (!impl_) {
        return;
    }

    if (impl_->codec) {
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

bool H264Decoder::decode(const uint8_t* data, size_t len) {
    if (!impl_ || !impl_->codec || !impl_->packet || !data || len == 0) {
        return false;
    }

    av_packet_unref(impl_->packet);
    if (av_new_packet(impl_->packet, static_cast<int>(len)) < 0) {
        return false;
    }
    std::memcpy(impl_->packet->data, data, len);

    if (avcodec_send_packet(impl_->codec, impl_->packet) < 0) {
        return false;
    }

    while (true) {
        const int ret = avcodec_receive_frame(impl_->codec, impl_->frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            return false;
        }
        emitFrame(impl_, impl_->frame);
    }

    return true;
}

} // namespace openshare
