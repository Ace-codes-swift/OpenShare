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

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "env.hpp"
#include "hotp.hpp"
#include "mac_pairing.hpp"
#include "net.hpp"
#include "protocol.hpp"
#include "vt_decoder.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace openshare;
using namespace openshare::protocol;

namespace {

struct AppState {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;

    std::unique_ptr<TcpSocket> socket;
    VtDecoder decoder;

    std::mutex frameMutex;
    std::vector<uint8_t> frameBgra;
    int frameW = 0;
    int frameH = 0;
    bool frameDirty = false;

    std::atomic<bool> connected{false};
    std::thread recvThread;

    uint32_t remoteW = 1920;
    uint32_t remoteH = 1080;

    float lastNx = 0.5f;
    float lastNy = 0.5f;
};

AppState* g = nullptr;

uint32_t sdlModsToProtocol(SDL_Keymod mods) {
    uint32_t m = 0;
    if (mods & SDL_KMOD_SHIFT) {
        m |= kModShift;
    }
    if (mods & SDL_KMOD_CTRL) {
        m |= kModCtrl;
    }
    if (mods & SDL_KMOD_ALT) {
        m |= kModAlt;
    }
    if (mods & SDL_KMOD_GUI) {
        m |= kModCmd;
    }
    return m;
}

// Map common SDL scancodes to macOS virtual key codes for injection.
uint16_t sdlToMacKeycode(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_A:
        return 0x00;
    case SDL_SCANCODE_S:
        return 0x01;
    case SDL_SCANCODE_D:
        return 0x02;
    case SDL_SCANCODE_F:
        return 0x03;
    case SDL_SCANCODE_H:
        return 0x04;
    case SDL_SCANCODE_G:
        return 0x05;
    case SDL_SCANCODE_Z:
        return 0x06;
    case SDL_SCANCODE_X:
        return 0x07;
    case SDL_SCANCODE_C:
        return 0x08;
    case SDL_SCANCODE_V:
        return 0x09;
    case SDL_SCANCODE_B:
        return 0x0B;
    case SDL_SCANCODE_Q:
        return 0x0C;
    case SDL_SCANCODE_W:
        return 0x0D;
    case SDL_SCANCODE_E:
        return 0x0E;
    case SDL_SCANCODE_R:
        return 0x0F;
    case SDL_SCANCODE_Y:
        return 0x10;
    case SDL_SCANCODE_T:
        return 0x11;
    case SDL_SCANCODE_1:
        return 0x12;
    case SDL_SCANCODE_2:
        return 0x13;
    case SDL_SCANCODE_3:
        return 0x14;
    case SDL_SCANCODE_4:
        return 0x15;
    case SDL_SCANCODE_6:
        return 0x16;
    case SDL_SCANCODE_5:
        return 0x17;
    case SDL_SCANCODE_EQUALS:
        return 0x18;
    case SDL_SCANCODE_9:
        return 0x19;
    case SDL_SCANCODE_7:
        return 0x1A;
    case SDL_SCANCODE_MINUS:
        return 0x1B;
    case SDL_SCANCODE_8:
        return 0x1C;
    case SDL_SCANCODE_0:
        return 0x1D;
    case SDL_SCANCODE_RIGHTBRACKET:
        return 0x1E;
    case SDL_SCANCODE_O:
        return 0x1F;
    case SDL_SCANCODE_U:
        return 0x20;
    case SDL_SCANCODE_LEFTBRACKET:
        return 0x21;
    case SDL_SCANCODE_I:
        return 0x22;
    case SDL_SCANCODE_P:
        return 0x23;
    case SDL_SCANCODE_RETURN:
        return 0x24;
    case SDL_SCANCODE_L:
        return 0x25;
    case SDL_SCANCODE_J:
        return 0x26;
    case SDL_SCANCODE_APOSTROPHE:
        return 0x27;
    case SDL_SCANCODE_K:
        return 0x28;
    case SDL_SCANCODE_SEMICOLON:
        return 0x29;
    case SDL_SCANCODE_BACKSLASH:
        return 0x2A;
    case SDL_SCANCODE_COMMA:
        return 0x2B;
    case SDL_SCANCODE_SLASH:
        return 0x2C;
    case SDL_SCANCODE_N:
        return 0x2D;
    case SDL_SCANCODE_M:
        return 0x2E;
    case SDL_SCANCODE_PERIOD:
        return 0x2F;
    case SDL_SCANCODE_TAB:
        return 0x30;
    case SDL_SCANCODE_SPACE:
        return 0x31;
    case SDL_SCANCODE_GRAVE:
        return 0x32;
    case SDL_SCANCODE_BACKSPACE:
        return 0x33;
    case SDL_SCANCODE_ESCAPE:
        return 0x35;
    case SDL_SCANCODE_LGUI:
    case SDL_SCANCODE_RGUI:
        return 0x37;
    case SDL_SCANCODE_LSHIFT:
        return 0x38;
    case SDL_SCANCODE_CAPSLOCK:
        return 0x39;
    case SDL_SCANCODE_LALT:
        return 0x3A;
    case SDL_SCANCODE_LCTRL:
        return 0x3B;
    case SDL_SCANCODE_RSHIFT:
        return 0x3C;
    case SDL_SCANCODE_RALT:
        return 0x3D;
    case SDL_SCANCODE_RCTRL:
        return 0x3E;
    case SDL_SCANCODE_LEFT:
        return 0x7B;
    case SDL_SCANCODE_RIGHT:
        return 0x7C;
    case SDL_SCANCODE_DOWN:
        return 0x7D;
    case SDL_SCANCODE_UP:
        return 0x7E;
    case SDL_SCANCODE_DELETE:
        return 0x75;
    case SDL_SCANCODE_F1:
        return 0x7A;
    case SDL_SCANCODE_F2:
        return 0x78;
    case SDL_SCANCODE_F3:
        return 0x63;
    case SDL_SCANCODE_F4:
        return 0x76;
    case SDL_SCANCODE_F5:
        return 0x60;
    case SDL_SCANCODE_F6:
        return 0x61;
    case SDL_SCANCODE_F7:
        return 0x62;
    case SDL_SCANCODE_F8:
        return 0x64;
    case SDL_SCANCODE_F9:
        return 0x65;
    case SDL_SCANCODE_F10:
        return 0x6D;
    case SDL_SCANCODE_F11:
        return 0x67;
    case SDL_SCANCODE_F12:
        return 0x6F;
    default:
        return 0xFFFF;
    }
}

bool normalizeMouse(AppState* app, float winX, float winY, float& nx, float& ny) {
    int w = 0, h = 0;
    SDL_GetWindowSize(app->window, &w, &h);
    if (w <= 0 || h <= 0) {
        return false;
    }
    nx = winX / static_cast<float>(w);
    ny = winY / static_cast<float>(h);
    if (nx < 0.f) {
        nx = 0.f;
    }
    if (nx > 1.f) {
        nx = 1.f;
    }
    if (ny < 0.f) {
        ny = 0.f;
    }
    if (ny > 1.f) {
        ny = 1.f;
    }
    return true;
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char** /*argv*/) {
    Env::loadDefaults();

    auto* app = new AppState();
    *appstate = app;
    g = app;

    SDL_SetAppMetadata("OpenShare", "1.0", "com.atech.OpenShare");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    const uint16_t port = static_cast<uint16_t>(Env::getInt("OPENSHARE_PORT", 9000));

    // Companion regenerates its manager code on every manual launch, so always
    // ask for the current address + code (prefilling the last successful pair).
    auto existing = loadManagerPairing();
    auto pairing = repromptManagerPairing(existing ? existing->host : "",
                                          existing ? existing->code : "");
    if (!pairing) {
        SDL_Log("Pairing was cancelled or the manager code could not be stored in Keychain.");
        return SDL_APP_FAILURE;
    }
    Hotp::saveCounter("hotp_counter_manager", 0);

    // An explicit OPENSHARE_HOST env/.env value overrides the paired address.
    const std::string hostOverride = Env::get("OPENSHARE_HOST", "");

    // Pairing loop. Each attempt connects and authenticates:
    //   - unreachable host          -> re-prompt for address (prefilled)
    //   - counter drift (car-key)   -> auto-resync to the worker's counter
    //   - wrong manager code        -> re-prompt for the code and try again
    Hotp hotp(Hotp::deriveSecret(pairing->code));
    std::optional<TcpSocket> sock;
    bool ok = false;
    uint32_t rw = 0, rh = 0;

    auto connectToWorker = [&]() -> std::optional<TcpSocket> {
        std::string host = hostOverride;
        if (host.empty()) {
            host = pairing->host;
        }
        if (host.empty()) {
            host = "127.0.0.1";
        }
        std::cout << "Connecting to " << host << ":" << port << "...\n";
        auto s = TcpSocket::connect(host, port);
        if (s) {
            // Never block forever on a wedged/dead worker: bound the
            // handshake and detect silently-dropped connections.
            s->setRecvTimeout(10000);
            s->setSendTimeout(10000);
            s->enableKeepalive();
        }
        return s;
    };

    for (int pairAttempt = 0; pairAttempt < 4 && !ok; ++pairAttempt) {
        if (pairAttempt > 0) {
            auto redo = repromptManagerPairing(pairing->host, pairing->code);
            if (!redo) {
                return SDL_APP_FAILURE;
            }
            *pairing = *redo;
            hotp = Hotp(Hotp::deriveSecret(pairing->code));
        }

        sock = connectToWorker();
        if (!sock) {
            SDL_Log("Failed to connect (attempt %d)", pairAttempt + 1);
            if (!hostOverride.empty()) {
                showPairingError("Could not reach the OPENSHARE_HOST override address.\n"
                                 "Make sure OpenShareCompanion is running there.");
                return SDL_APP_FAILURE;
            }
            showPairingError("Could not reach the worker Mac.\nMake sure "
                             "OpenShareCompanion is running on it, both Macs are on "
                             "the same network, and the address is correct.");
            continue;
        }

        // Auth with counter resync: a rejection carries the worker's expected
        // counter, so a viewer with the right code can catch up instead of
        // being locked out forever.
        for (int authAttempt = 0; authAttempt < 3; ++authAttempt) {
            const uint64_t counter = Hotp::loadCounter("hotp_counter_manager");
            if (!sock->sendAll(encodeAuth(counter, hotp.codeAt(counter)))) {
                SDL_Log("Failed to send auth.");
                break;
            }

            auto authResult = sock->recvMessage();
            if (!authResult || authResult->type != MsgType::AuthResult) {
                SDL_Log("No AuthResult from companion (timeout or disconnect).");
                showPairingError("The worker Mac accepted the connection but never "
                                 "replied in time.\nQuit OpenShareCompanion on the "
                                 "worker Mac completely (Activity Monitor if needed), "
                                 "install the latest Companion DMG, launch it, then "
                                 "try again with the new code it shows.");
                return SDL_APP_FAILURE;
            }

            uint64_t expectedCounter = 0;
            if (!parseAuthResult(authResult->payload, ok, rw, rh, expectedCounter)) {
                SDL_Log("Invalid AuthResult from companion.");
                break;
            }
            if (ok) {
                Hotp::saveCounter("hotp_counter_manager", counter + 1);
                break;
            }

            SDL_Log("Auth rejected at counter %llu; worker expects %llu.",
                    static_cast<unsigned long long>(counter),
                    static_cast<unsigned long long>(expectedCounter));
            if (expectedCounter <= counter) {
                // Counters already agreed, so the code itself is wrong;
                // resyncing cannot help. Fall through to re-prompt.
                break;
            }
            Hotp::saveCounter("hotp_counter_manager", expectedCounter);

            // The worker drops the connection after a failed auth; reconnect.
            sock = connectToWorker();
            if (!sock) {
                break;
            }
        }

        if (!ok && pairAttempt == 0) {
            showPairingError("Pairing was rejected by the worker Mac — the manager "
                             "code does not match.\nOpen OpenShareCompanion on the "
                             "worker Mac to see its current code, then enter it here.");
        }
    }

    if (!ok || !sock) {
        SDL_Log("Authentication failed (manager code mismatch).");
        showPairingError("Could not pair with the worker Mac after several tries.");
        return SDL_APP_FAILURE;
    }

    app->remoteW = rw ? rw : 1920;
    app->remoteH = rh ? rh : 1080;
    // Streaming can legitimately go quiet (static remote screen sends no
    // frames), so drop the recv timeout; keepalive still detects dead peers.
    sock->setRecvTimeout(0);
    app->socket = std::make_unique<TcpSocket>(std::move(*sock));
    app->connected = true;

    std::cout << "Authenticated. Remote video " << app->remoteW << "x" << app->remoteH << "\n";

    if (!SDL_CreateWindowAndRenderer("OpenShare",
                                     static_cast<int>(app->remoteW),
                                     static_cast<int>(app->remoteH),
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &app->window,
                                     &app->renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Remote cursor is painted into the video on the companion. Hide the local
    // OS cursor over this window so you only see the real host pointer.
    SDL_HideCursor();

    // Capture the pointer by value: this callback fires on VideoToolbox's
    // decoder queue long after SDL_AppInit's stack frame is gone.
    app->decoder.start([app](const uint8_t* bgra, int width, int height, size_t /*stride*/) {
        std::lock_guard<std::mutex> lock(app->frameMutex);
        app->frameBgra.assign(bgra, bgra + static_cast<size_t>(width) * height * 4);
        app->frameW = width;
        app->frameH = height;
        app->frameDirty = true;
    });

    app->recvThread = std::thread([app]() {
        while (app->connected && app->socket && app->socket->valid()) {
            auto msg = app->socket->recvMessage();
            if (!msg) {
                app->connected = false;
                break;
            }
            if (msg->type == MsgType::Video) {
                app->decoder.decode(msg->payload.data(), msg->payload.size());
            }
            // CursorImage from older companions is ignored — cursor is in-frame.
        }
        app->connected = false;
    });

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto* app = static_cast<AppState*>(appstate);

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (!app->connected || !app->socket) {
        return SDL_APP_CONTINUE;
    }

    switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION: {
        float nx = 0, ny = 0;
        if (normalizeMouse(app, event->motion.x, event->motion.y, nx, ny)) {
            app->lastNx = nx;
            app->lastNy = ny;
            app->socket->sendAll(encodeMouseMove(nx, ny));
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        float nx = 0, ny = 0;
        normalizeMouse(app, event->button.x, event->button.y, nx, ny);
        app->lastNx = nx;
        app->lastNy = ny;
        uint8_t button = 0;
        if (event->button.button == SDL_BUTTON_RIGHT) {
            button = 1;
        } else if (event->button.button == SDL_BUTTON_MIDDLE) {
            button = 2;
        }
        const uint8_t down = (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? 1 : 0;
        app->socket->sendAll(encodeMouseButton(button, down));
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        // Trackpads deliver many small fractional deltas; truncating to int
        // dropped nearly all of them. Convert to pixel deltas (injected with
        // pixel-unit scroll events on the worker) and respect macOS "natural"
        // scrolling, which SDL reports as a flipped direction.
        float fx = event->wheel.x;
        float fy = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            fx = -fx;
            fy = -fy;
        }
        constexpr float kPixelsPerTick = 32.0f;
        const int32_t dx = static_cast<int32_t>(std::lround(fx * kPixelsPerTick));
        const int32_t dy = static_cast<int32_t>(std::lround(fy * kPixelsPerTick));
        if (dx != 0 || dy != 0) {
            app->socket->sendAll(encodeMouseWheel(dx, dy));
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const uint16_t macKey = sdlToMacKeycode(event->key.scancode);
        if (macKey == 0xFFFF) {
            break;
        }
        const uint8_t down = (event->type == SDL_EVENT_KEY_DOWN) ? 1 : 0;
        if (event->key.repeat) {
            break;
        }
        app->socket->sendAll(encodeKey(macKey, sdlModsToProtocol(SDL_GetModState()), down));
        break;
    }
    default:
        break;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = static_cast<AppState*>(appstate);

    if (!app->connected) {
        SDL_Log("Disconnected from companion.");
        return SDL_APP_SUCCESS;
    }

    {
        std::lock_guard<std::mutex> lock(app->frameMutex);
        if (app->frameDirty && !app->frameBgra.empty()) {
            float texW = 0, texH = 0;
            if (app->texture) {
                SDL_GetTextureSize(app->texture, &texW, &texH);
            }
            if (!app->texture || static_cast<int>(texW) != app->frameW ||
                static_cast<int>(texH) != app->frameH) {
                SDL_DestroyTexture(app->texture);
                app->texture = SDL_CreateTexture(app->renderer,
                                                 SDL_PIXELFORMAT_BGRA32,
                                                 SDL_TEXTUREACCESS_STREAMING,
                                                 app->frameW,
                                                 app->frameH);
            }
            if (app->texture) {
                SDL_UpdateTexture(app->texture, nullptr, app->frameBgra.data(), app->frameW * 4);
            }
            app->frameDirty = false;
        }
    }

    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    if (app->texture) {
        SDL_RenderTexture(app->renderer, app->texture, nullptr, nullptr);
    }
    SDL_RenderPresent(app->renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/) {
    auto* app = static_cast<AppState*>(appstate);
    if (!app) {
        return;
    }

    app->connected = false;
    if (app->socket) {
        app->socket->close();
    }
    if (app->recvThread.joinable()) {
        app->recvThread.join();
    }
    app->decoder.stop();
    if (app->texture) {
        SDL_DestroyTexture(app->texture);
    }
    SDL_ShowCursor();
    delete app;
    g = nullptr;
}
