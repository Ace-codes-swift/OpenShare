#include "env.hpp"
#include "hotp.hpp"
#include "mac_pairing.hpp"
#include "mac_inject.hpp"
#include "net.hpp"
#include "protocol.hpp"
#include "vt_encoder.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ScreenCapture.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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

// Quit every other running OpenShareCompanion so a fresh manual launch can
// take over the listen port. Also unload the LaunchAgent for this session —
// otherwise KeepAlive would immediately relaunch the old instance and steal
// the port back.
void quitOtherCompanionInstances() {
    // Best-effort: unload the installed LaunchAgent for this login session.
    std::system("launchctl bootout gui/$(id -u)/com.atech.OpenShareCompanion "
                ">/dev/null 2>&1");

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
    // Give the previous process a moment to release the port.
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

// Alpha-blend a BGRA cursor onto a BGRA frame. destX/destY are the top-left
// of the cursor bitmap in frame pixel coordinates.
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

} // namespace

int main() {
    Env::loadDefaults();

    // Check permissions before anything else: without them the companion
    // captures nothing and injects nothing, which looks like it "does nothing".
    if (!ensureCompanionPermissions()) {
        std::cerr << "Missing Screen Recording and/or Accessibility permission. "
                     "Grant them in System Settings, then relaunch.\n";
        return 1;
    }

    const bool serviceMode = Env::get("OPENSHARE_SERVICE", "") == "1";
    const uint16_t port = static_cast<uint16_t>(Env::getInt("OPENSHARE_PORT", 9000));
    // 0 = stream at the native capture resolution (sharp on Retina).
    // Forcing a smaller size means a blurry nearest-neighbor downscale.
    const int videoW = Env::getInt("OPENSHARE_VIDEO_WIDTH", 0);
    const int videoH = Env::getInt("OPENSHARE_VIDEO_HEIGHT", 0);
    const int fps = Env::getInt("OPENSHARE_TARGET_FPS", 30);
    // Max counter jump we accept ahead of our stored value (drift tolerance).
    const uint64_t lookAhead = static_cast<uint64_t>(Env::getInt("OPENSHARE_COUNTER_WINDOW", 1000));

    // On a manual launch, replace any already-running companion (old service
    // or stuck prior instance). Previously a failed bind just showed a dialog
    // and exited — so closing the dialog "quit the worker" while the real
    // (stale) listener kept rejecting every pairing attempt.
    if (!serviceMode) {
        quitOtherCompanionInstances();
    }

    auto listener = TcpSocket::listen(port);
    if (!listener && !serviceMode) {
        // One more try after a longer pause in case the previous process was slow.
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        quitOtherCompanionInstances();
        listener = TcpSocket::listen(port);
    }
    if (!listener) {
        std::cerr << "Failed to listen on port " << port << "\n";
        if (!serviceMode) {
            showPairingError("OpenShareCompanion could not take over port " +
                             std::to_string(port) +
                             ".\nQuit any other OpenShareCompanion in Activity "
                             "Monitor, then launch again.");
        }
        return 1;
    }

    std::string managerCode;
    if (serviceMode) {
        // Service: keep the stored code stable across logins/restarts.
        auto pairing = loadOrCreateWorkerPairingCode();
        if (!pairing) {
            std::cerr << "Could not create or load the manager code from Keychain.\n";
            return 1;
        }
        if (pairing->created) {
            Hotp::saveCounter("hotp_counter_worker", 0);
        }
        managerCode = pairing->value;
    } else {
        // Manual launch: always start a fresh pairing with a new code.
        auto code = regenerateWorkerPairingCode();
        if (!code) {
            std::cerr << "Could not store a new manager code in Keychain.\n";
            return 1;
        }
        Hotp::saveCounter("hotp_counter_worker", 0);
        managerCode = *code;
    }

    Hotp hotp(Hotp::deriveSecret(managerCode));
    // Last counter we accepted + 1; any replayed (older) worker counter is rejected.
    uint64_t nextCounter = Hotp::loadCounter("hotp_counter_worker");

    std::cout << "OpenShareCompanion listening on 0.0.0.0:" << port << "\n";
    std::cout << "Grant Screen Recording + Accessibility permissions if prompted.\n";

    std::atomic<bool> running{true};

    int hostScreenW = 0;
    int hostScreenH = 0;
    float lastNx = 0.5f;
    float lastNy = 0.5f;
    MacInputInjector injector;

    // Retina backing scale: capture is in pixels, cursors/CGEvents in points.
    const CGRect mainBounds = CGDisplayBounds(CGMainDisplayID());
    double backingScale = 1.0;
    if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode(CGMainDisplayID())) {
        const size_t pixelW = CGDisplayModeGetPixelWidth(mode);
        const size_t pointW = CGDisplayModeGetWidth(mode);
        if (pixelW > 0 && pointW > 0) {
            backingScale = static_cast<double>(pixelW) / static_cast<double>(pointW);
        }
        CGDisplayModeRelease(mode);
    }
    std::cout << "Display backing scale: " << backingScale << "\n";

    // Manual launches show the pairing dialog AFTER we start accepting, so the
    // worker is already live while the user copies the code. Without this the
    // viewer connected into a listen backlog that was never drained until the
    // dialog closed — then the session often already timed out.
    std::thread sessionThread([&]() {
    while (running) {
        std::cout << "\nWaiting for viewer...\n";
        auto client = listener->accept();
        if (!client) {
            continue;
        }
        std::cout << "Client connected, awaiting auth...\n";

        // A dead or stalled peer must never wedge the (single-session) accept
        // loop: bound every wait and probe the connection with keepalives.
        client->enableKeepalive();
        client->setRecvTimeout(10000);
        client->setSendTimeout(10000);

        auto authMsg = client->recvMessage();
        if (!authMsg || authMsg->type != MsgType::Auth) {
            // Dead/zombie connection from a previous timed-out viewer. Reply if
            // we can, then move on — never sit silent.
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

        // Rolling-code check: counter must be new (anti-replay) and within
        // the look-ahead window, and the code must match at that counter.
        // Failures echo our expected counter so a viewer with the right
        // secret can resync instead of being locked out forever.
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

        // Reply BEFORE starting capture. Screen Recording setup can hang for a
        // long time (or forever) when TCC/permissions are weird — if we delay
        // AuthResult until after that, the viewer times out with "accepted but
        // never replied" even though auth itself succeeded.
        // The advertised size only seeds the viewer's window; the texture
        // adapts to whatever resolution the decoded frames actually have.
        const uint32_t advertiseW = static_cast<uint32_t>(
            videoW > 0 ? videoW : mainBounds.size.width);
        const uint32_t advertiseH = static_cast<uint32_t>(
            videoH > 0 ? videoH : mainBounds.size.height);
        if (!client->sendAll(encodeAuthResult(true, advertiseW, advertiseH))) {
            continue;
        }
        // Streaming can idle; keepalives still detect a dead viewer.
        client->setRecvTimeout(0);

        // Capture setup
        std::mutex frameMutex;
        std::vector<unsigned char> frameBuffer;
        int frameW = 0;
        int frameH = 0;
        bool frameDirty = false;
        std::atomic<bool> sessionActive{true};
        std::mutex sendMutex;

        // Host cursor stays visible on the worker Mac and is painted into the
        // video frames. We do NOT send a separate CursorImage to the viewer
        // (that path was Retina-broken: huge / soft / wrong hotspot).
        std::mutex cursorMutex;
        std::vector<uint8_t> cursorBgra;
        int cursorW = 0;
        int cursorH = 0;
        int cursorHotX = 0;
        int cursorHotY = 0;
        // Position of the hotspot in capture-pixel coordinates.
        int cursorPx = 0;
        int cursorPy = 0;
        bool cursorHaveImage = false;

        auto captureManager =
            SL::Screen_Capture::CreateCaptureConfiguration([]() {
                auto monitors = SL::Screen_Capture::GetMonitors();
                if (!monitors.empty()) {
                    monitors.resize(1);
                }
                return monitors;
            })
                ->onNewFrame([&](const SL::Screen_Capture::Image& img,
                                const SL::Screen_Capture::Monitor& /*monitor*/) {
                    const int width = Width(img);
                    const int height = Height(img);
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
                    // CGEvent location is in global points; capture is pixels.
                    const int px = static_cast<int>(
                        (mouse.Position.x - mainBounds.origin.x) * backingScale);
                    const int py = static_cast<int>(
                        (mouse.Position.y - mainBounds.origin.y) * backingScale);

                    std::lock_guard<std::mutex> lock(cursorMutex);
                    cursorPx = px;
                    cursorPy = py;
                    if (!img) {
                        return;
                    }
                    const int width = Width(*img);
                    const int height = Height(*img);
                    if (width <= 0 || height <= 0 || width > 512 || height > 512) {
                        return;
                    }
                    cursorBgra.resize(static_cast<size_t>(width) * height *
                                      sizeof(SL::Screen_Capture::ImageBGRA));
                    SL::Screen_Capture::Extract(*img, cursorBgra.data(), cursorBgra.size());
                    cursorW = width;
                    cursorH = height;
                    cursorHotX = std::clamp(mouse.HotSpot.x, 0, width - 1);
                    cursorHotY = std::clamp(mouse.HotSpot.y, 0, height - 1);
                    cursorHaveImage = true;
                })
                ->start_capturing();

        const int intervalMs = fps > 0 ? (1000 / fps) : 33;
        captureManager->setFrameChangeInterval(std::chrono::milliseconds(intervalMs));
        captureManager->setMouseChangeInterval(std::chrono::milliseconds(16));

        // Wait briefly for first frame to learn screen size (non-fatal if none).
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

        // Encode at the native capture resolution unless the .env pins a size.
        // H.264 needs even dimensions.
        int encodeW = videoW > 0 ? videoW : (hostScreenW > 0 ? hostScreenW & ~1 : 1920);
        int encodeH = videoH > 0 ? videoH : (hostScreenH > 0 ? hostScreenH & ~1 : 1080);
        std::cout << "Streaming at " << encodeW << "x" << encodeH << "\n";

        VtEncoder encoder;
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
            std::cerr << "Failed to start VideoToolbox encoder.\n";
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
                    if (!frameDirty || frameBuffer.empty()) {
                        // fall through to sleep
                    } else {
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
                        // Native mode with odd pixel row/col trimmed: crop, don't blur.
                        for (int y = 0; y < encodeH; ++y) {
                            std::memcpy(scaled.data() + static_cast<size_t>(y) * encodeW * 4,
                                        local.data() + static_cast<size_t>(y) * w * 4,
                                        static_cast<size_t>(encodeW) * 4);
                        }
                    } else {
                        scaleBgra(local.data(), w, h, scaled.data(), encodeW, encodeH);
                    }

                    // Paint the real host cursor into the frame so the viewer
                    // never needs a separate (Retina-broken) cursor overlay.
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
                            // Map capture-pixel hotspot into encode space if we scaled.
                            const int mappedX =
                                (w > 0) ? (cpx * encodeW) / w : cpx;
                            const int mappedY =
                                (h > 0) ? (cpy * encodeH) / h : cpy;
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

        // Input loop on this thread
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
                    injector.mouseMove(x, y, hostScreenW, hostScreenH);
                }
                break;
            }
            case MsgType::MouseButton: {
                uint8_t button = 0, down = 0;
                if (parseMouseButton(msg->payload, button, down)) {
                    injector.mouseButton(button, down != 0, lastNx, lastNy, hostScreenW, hostScreenH);
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
    }); // sessionThread

    if (!serviceMode) {
        // Dialog blocks the main thread; the session thread is already accepting.
        showWorkerReadyDialog(managerCode);
    }

    // Keep pumping the AppKit/CF run loop on the main thread. While the
    // pairing alert was open, runModal did this for us. After "Done", the old
    // code called sessionThread.join() which parked main with no run loop —
    // ScreenCapture/TCC/AppKit cleanup then crashed, even though the worker
    // thread kept accepting connections ("crashes but still works").
    while (running.load()) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);
    }

    if (sessionThread.joinable()) {
        sessionThread.join();
    }
    return 0;
}
