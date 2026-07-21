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

#include "env.hpp"
#include "hotp.hpp"
#include "h264_encoder.hpp"
#include "net.hpp"
#include "pairing.hpp"
#include "protocol.hpp"
#include "x11_inject.hpp"

#include <ScreenCapture.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace openshare;
using namespace openshare::protocol;

namespace {

void quitOtherCompanionInstances() {
    (void)std::system("systemctl --user stop openshare-companion.service >/dev/null 2>&1");

    FILE* pipe = popen("pgrep -x OpenShareCompanion", "r");
    if (!pipe) {
        return;
    }
    const pid_t self = getpid();
    char line[64];
    while (fgets(line, sizeof(line), pipe)) {
        const pid_t pid = static_cast<pid_t>(std::atoi(line));
        if (pid > 0 && pid != self) {
            std::cout << "Stopping previous OpenShareCompanion (pid " << pid << ")\n";
            kill(pid, SIGTERM);
        }
    }
    pclose(pipe);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
}

void scaleBgra(const uint8_t* src,
               int srcW,
               int srcH,
               uint8_t* dst,
               int dstW,
               int dstH) {
    for (int y = 0; y < dstH; ++y) {
        const int sy = y * srcH / dstH;
        for (int x = 0; x < dstW; ++x) {
            const int sx = x * srcW / dstW;
            const uint8_t* s = src + (static_cast<size_t>(sy) * srcW + sx) * 4;
            uint8_t* d = dst + (static_cast<size_t>(y) * dstW + x) * 4;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
    }
}

void blitCursorBgra(uint8_t* frame,
                    int frameW,
                    int frameH,
                    const uint8_t* cursor,
                    int cursorW,
                    int cursorH,
                    int destX,
                    int destY) {
    for (int y = 0; y < cursorH; ++y) {
        const int fy = destY + y;
        if (fy < 0 || fy >= frameH) {
            continue;
        }
        for (int x = 0; x < cursorW; ++x) {
            const int fx = destX + x;
            if (fx < 0 || fx >= frameW) {
                continue;
            }
            const uint8_t* s = cursor + (static_cast<size_t>(y) * cursorW + x) * 4;
            uint8_t* d = frame + (static_cast<size_t>(fy) * frameW + fx) * 4;
            const unsigned a = s[3];
            if (a == 0) {
                continue;
            }
            if (a == 255) {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
                continue;
            }
            const unsigned ia = 255 - a;
            d[0] = static_cast<uint8_t>((s[0] * a + d[0] * ia) / 255);
            d[1] = static_cast<uint8_t>((s[1] * a + d[1] * ia) / 255);
            d[2] = static_cast<uint8_t>((s[2] * a + d[2] * ia) / 255);
            d[3] = 255;
        }
    }
}

std::pair<int, int> primaryDisplaySize() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return {1920, 1080};
    }
    const int screen = DefaultScreen(display);
    const int w = DisplayWidth(display, screen);
    const int h = DisplayHeight(display, screen);
    XCloseDisplay(display);
    return {w > 0 ? w : 1920, h > 0 ? h : 1080};
}

} // namespace

int main() {
    Env::loadDefaults();

    const bool serviceMode = Env::get("OPENSHARE_SERVICE", "") == "1";
    const uint16_t port = static_cast<uint16_t>(Env::getInt("OPENSHARE_PORT", 9000));
    const int videoW = Env::getInt("OPENSHARE_VIDEO_WIDTH", 0);
    const int videoH = Env::getInt("OPENSHARE_VIDEO_HEIGHT", 0);
    const int fps = Env::getInt("OPENSHARE_TARGET_FPS", 30);
    const uint64_t lookAhead = static_cast<uint64_t>(Env::getInt("OPENSHARE_COUNTER_WINDOW", 1000));

    if (!serviceMode) {
        quitOtherCompanionInstances();
    }

    auto listener = TcpSocket::listen(port);
    if (!listener && !serviceMode) {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        quitOtherCompanionInstances();
        listener = TcpSocket::listen(port);
    }
    if (!listener) {
        std::cerr << "Failed to listen on port " << port << "\n";
        if (!serviceMode) {
            showPairingError("OpenShareCompanion could not take over port " +
                             std::to_string(port) +
                             ".\nStop any other OpenShareCompanion, then launch again.");
        }
        return 1;
    }

    std::string managerCode;
    if (serviceMode) {
        auto pairing = loadOrCreateWorkerPairingCode();
        if (!pairing) {
            std::cerr << "Could not create or load the manager code from ~/.openshare/.\n";
            return 1;
        }
        if (pairing->created) {
            Hotp::saveCounter("hotp_counter_worker", 0);
        }
        managerCode = pairing->value;
    } else {
        auto code = regenerateWorkerPairingCode();
        if (!code) {
            std::cerr << "Could not store a new manager code in ~/.openshare/.\n";
            return 1;
        }
        Hotp::saveCounter("hotp_counter_worker", 0);
        managerCode = *code;
    }

    Hotp hotp(Hotp::deriveSecret(managerCode));
    uint64_t nextCounter = Hotp::loadCounter("hotp_counter_worker");

    const auto [displayW, displayH] = primaryDisplaySize();

    std::cout << "OpenShareCompanion listening on 0.0.0.0:" << port << "\n";

    int hostScreenW = 0;
    int hostScreenH = 0;
    float lastNx = 0.5f;
    float lastNy = 0.5f;
    X11Injector injector;
    if (!injector.ready()) {
        std::cerr << "Could not open X11 display for input injection.\n";
        return 1;
    }

    std::thread sessionThread([&]() {
        while (true) {
            std::cout << "\nWaiting for viewer...\n";
            auto client = listener->accept();
            if (!client) {
                continue;
            }
            std::cout << "Client connected, awaiting auth...\n";

            client->enableKeepalive();
            client->setRecvTimeout(10000);
            client->setSendTimeout(10000);

            auto authMsg = client->recvMessage();
            if (!authMsg || authMsg->type != MsgType::Auth) {
                std::cerr << "Expected Auth message (stale or empty connection).\n";
                client->sendAll(encodeAuthResult(false, 0, 0, nextCounter));
                continue;
            }

            uint8_t version = 0;
            uint64_t claimedCounter = 0;
            std::string code;
            if (!parseAuth(authMsg->payload, version, claimedCounter, code) || version != kVersion) {
                client->sendAll(encodeAuthResult(false, 0, 0, nextCounter));
                continue;
            }

            if (claimedCounter < nextCounter || claimedCounter > nextCounter + lookAhead) {
                std::cerr << "Auth rejected: counter " << claimedCounter << " outside window ["
                          << nextCounter << ", " << nextCounter + lookAhead << "].\n";
                client->sendAll(encodeAuthResult(false, 0, 0, nextCounter));
                continue;
            }
            if (hotp.codeAt(claimedCounter) != code) {
                std::cerr << "Auth rejected: bad rolling code at counter " << claimedCounter << ".\n";
                client->sendAll(encodeAuthResult(false, 0, 0, nextCounter));
                continue;
            }

            nextCounter = claimedCounter + 1;
            Hotp::saveCounter("hotp_counter_worker", nextCounter);
            std::cout << "Auth OK. Starting session (next worker counter=" << nextCounter << ").\n";

            const uint32_t advertiseW =
                static_cast<uint32_t>(videoW > 0 ? videoW : displayW);
            const uint32_t advertiseH =
                static_cast<uint32_t>(videoH > 0 ? videoH : displayH);
            if (!client->sendAll(encodeAuthResult(true, advertiseW, advertiseH))) {
                continue;
            }
            client->setRecvTimeout(0);

            std::mutex frameMutex;
            std::vector<unsigned char> frameBuffer;
            int frameW = 0;
            int frameH = 0;
            bool frameDirty = false;
            std::atomic<bool> sessionActive{true};
            std::mutex sendMutex;

            std::mutex cursorMutex;
            std::vector<uint8_t> cursorBgra;
            int cursorW = 0;
            int cursorH = 0;
            int cursorHotX = 0;
            int cursorHotY = 0;
            int cursorPx = 0;
            int cursorPy = 0;
            bool cursorHaveImage = false;
            int monitorOffsetX = 0;
            int monitorOffsetY = 0;

            auto captureManager =
                SL::Screen_Capture::CreateCaptureConfiguration([]() {
                    auto monitors = SL::Screen_Capture::GetMonitors();
                    if (!monitors.empty()) {
                        monitors.resize(1);
                    }
                    return monitors;
                })
                    ->onNewFrame([&](const SL::Screen_Capture::Image& img,
                                    const SL::Screen_Capture::Monitor& monitor) {
                        const int width = SL::Screen_Capture::Width(img);
                        const int height = SL::Screen_Capture::Height(img);
                        monitorOffsetX = SL::Screen_Capture::OffsetX(monitor);
                        monitorOffsetY = SL::Screen_Capture::OffsetY(monitor);
                        std::lock_guard<std::mutex> lock(frameMutex);
                        frameBuffer.resize(static_cast<size_t>(width) * height *
                                           sizeof(SL::Screen_Capture::ImageBGRA));
                        SL::Screen_Capture::Extract(img, frameBuffer.data(), frameBuffer.size());
                        frameW = width;
                        frameH = height;
                        frameDirty = true;
                    })
                    ->onMouseChanged([&](const SL::Screen_Capture::Image* img,
                                        const SL::Screen_Capture::MousePoint& mouse) {
                        if (!sessionActive) {
                            return;
                        }
                        std::lock_guard<std::mutex> lock(cursorMutex);
                        cursorPx = mouse.Position.x - monitorOffsetX;
                        cursorPy = mouse.Position.y - monitorOffsetY;
                        if (!img) {
                            return;
                        }
                        const int width = SL::Screen_Capture::Width(*img);
                        const int height = SL::Screen_Capture::Height(*img);
                        if (width <= 0 || height <= 0) {
                            return;
                        }
                        cursorBgra.resize(static_cast<size_t>(width) * height * 4);
                        SL::Screen_Capture::Extract(*img, cursorBgra.data(), cursorBgra.size());
                        cursorW = width;
                        cursorH = height;
                        cursorHotX = mouse.HotSpot.x;
                        cursorHotY = mouse.HotSpot.y;
                        cursorHaveImage = true;
                    })
                    ->start_capturing();

            const int intervalMs = fps > 0 ? (1000 / fps) : 33;
            captureManager->setFrameChangeInterval(std::chrono::milliseconds(intervalMs));
            captureManager->setMouseChangeInterval(std::chrono::milliseconds(16));

            for (int i = 0; i < 50; ++i) {
                {
                    std::lock_guard<std::mutex> lock(frameMutex);
                    if (frameW > 0 && frameH > 0) {
                        hostScreenW = frameW;
                        hostScreenH = frameH;
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            int encodeW = videoW > 0 ? videoW : (hostScreenW > 0 ? hostScreenW & ~1 : 1920);
            int encodeH = videoH > 0 ? videoH : (hostScreenH > 0 ? hostScreenH & ~1 : 1080);
            std::cout << "Streaming at " << encodeW << "x" << encodeH << "\n";

            H264Encoder encoder;
            bool encoderOk = encoder.start(encodeW, encodeH, fps, [&](const uint8_t* data, size_t len,
                                                                      bool /*keyframe*/) {
                std::lock_guard<std::mutex> lock(sendMutex);
                if (!sessionActive) {
                    return;
                }
                if (!client->sendAll(encodeVideo(data, len))) {
                    sessionActive = false;
                }
            });

            if (!encoderOk) {
                std::cerr << "Failed to start H.264 encoder.\n";
                sessionActive = false;
                captureManager.reset();
                continue;
            }

            std::vector<uint8_t> scaled(static_cast<size_t>(encodeW) * encodeH * 4);
            auto sessionStart = std::chrono::steady_clock::now();

            std::thread encodeThread([&]() {
                while (sessionActive) {
                    std::vector<unsigned char> local;
                    int w = 0;
                    int h = 0;
                    {
                        std::lock_guard<std::mutex> lock(frameMutex);
                        if (frameDirty && !frameBuffer.empty()) {
                            local = frameBuffer;
                            w = frameW;
                            h = frameH;
                            frameDirty = false;
                        }
                    }

                    if (!local.empty() && w > 0 && h > 0) {
                        if (w == encodeW && h == encodeH) {
                            std::memcpy(scaled.data(), local.data(), scaled.size());
                        } else if (w >= encodeW && h >= encodeH) {
                            for (int y = 0; y < encodeH; ++y) {
                                std::memcpy(scaled.data() + static_cast<size_t>(y) * encodeW * 4,
                                            local.data() + static_cast<size_t>(y) * w * 4,
                                            static_cast<size_t>(encodeW) * 4);
                            }
                        } else {
                            scaleBgra(local.data(), w, h, scaled.data(), encodeW, encodeH);
                        }

                        {
                            std::vector<uint8_t> cur;
                            int cw = 0, ch = 0, hx = 0, hy = 0, cpx = 0, cpy = 0;
                            bool have = false;
                            {
                                std::lock_guard<std::mutex> lock(cursorMutex);
                                have = cursorHaveImage;
                                if (have) {
                                    cur = cursorBgra;
                                    cw = cursorW;
                                    ch = cursorH;
                                    hx = cursorHotX;
                                    hy = cursorHotY;
                                    cpx = cursorPx;
                                    cpy = cursorPy;
                                }
                            }
                            if (have && !cur.empty() && cw > 0 && ch > 0) {
                                const int mappedX = (w > 0) ? (cpx * encodeW) / w : cpx;
                                const int mappedY = (h > 0) ? (cpy * encodeH) / h : cpy;
                                const int hotMappedX =
                                    (w > 0 && w != encodeW) ? (hx * encodeW) / w : hx;
                                const int hotMappedY =
                                    (h > 0 && h != encodeH) ? (hy * encodeH) / h : hy;
                                std::vector<uint8_t> curScaled;
                                const uint8_t* blitSrc = cur.data();
                                int blitW = cw;
                                int blitH = ch;
                                if (w != encodeW || h != encodeH) {
                                    blitW = std::max(1, (cw * encodeW) / std::max(1, w));
                                    blitH = std::max(1, (ch * encodeH) / std::max(1, h));
                                    curScaled.resize(static_cast<size_t>(blitW) * blitH * 4);
                                    scaleBgra(cur.data(), cw, ch, curScaled.data(), blitW, blitH);
                                    blitSrc = curScaled.data();
                                }
                                blitCursorBgra(scaled.data(),
                                               encodeW,
                                               encodeH,
                                               blitSrc,
                                               blitW,
                                               blitH,
                                               mappedX - hotMappedX,
                                               mappedY - hotMappedY);
                            }
                        }

                        const auto now = std::chrono::steady_clock::now();
                        const int64_t ptsMs =
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - sessionStart)
                                .count();
                        encoder.encodeFrame(scaled.data(), static_cast<size_t>(encodeW) * 4, ptsMs);
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
                }
            });

            while (sessionActive) {
                if (!client->waitReadable(50)) {
                    continue;
                }
                auto msg = client->recvMessage();
                if (!msg) {
                    sessionActive = false;
                    break;
                }

                switch (msg->type) {
                case MsgType::MouseMove: {
                    float x = 0, y = 0;
                    if (parseMouseMove(msg->payload, x, y)) {
                        lastNx = x;
                        lastNy = y;
                        injector.mouseMove(x, y);
                    }
                    break;
                }
                case MsgType::MouseButton: {
                    uint8_t button = 0, down = 0;
                    if (parseMouseButton(msg->payload, button, down)) {
                        injector.mouseButton(button, down != 0, lastNx, lastNy);
                    }
                    break;
                }
                case MsgType::MouseWheel: {
                    int32_t dx = 0, dy = 0;
                    if (parseMouseWheel(msg->payload, dx, dy)) {
                        injector.mouseWheel(dx, dy);
                    }
                    break;
                }
                case MsgType::Key: {
                    uint16_t keycode = 0;
                    uint32_t modifiers = 0;
                    uint8_t down = 0;
                    if (parseKey(msg->payload, keycode, modifiers, down)) {
                        injector.key(keycode, modifiers, down != 0);
                    }
                    break;
                }
                default:
                    break;
                }
            }

            sessionActive = false;
            encodeThread.join();
            encoder.stop();
            captureManager.reset();
            std::cout << "Session ended.\n";
        }
    });

    if (!serviceMode) {
        showWorkerReadyDialog(managerCode);
    }

    sessionThread.join();
    return 0;
}
