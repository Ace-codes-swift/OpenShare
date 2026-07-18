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

#include "openshare/client.hpp"

#include "hotp.hpp"
#include "net.hpp"
#include "protocol.hpp"
#include "vt_decoder.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace openshare {
namespace {

using namespace protocol;

class SessionImpl final : public Session {
public:
    SessionImpl(TcpSocket sock, uint32_t remoteW, uint32_t remoteH)
        : socket_(std::move(sock)), remoteW_(remoteW), remoteH_(remoteH) {
        connected_ = true;
        socket_.setRecvTimeout(0);
        socket_.enableKeepalive();

        decoder_.start([this](const uint8_t* bgra, int width, int height, size_t /*stride*/) {
            std::lock_guard<std::mutex> lock(frameMutex_);
            frame_.width = width;
            frame_.height = height;
            frame_.stride = width * 4;
            frame_.bgra.assign(bgra, bgra + static_cast<size_t>(width) * height * 4);
            frameDirty_ = true;
        });

        recvThread_ = std::thread([this]() { recvLoop(); });
    }

    ~SessionImpl() override {
        disconnect();
    }

    bool connected() const override {
        return connected_.load();
    }

    uint32_t remoteWidth() const override {
        return remoteW_;
    }

    uint32_t remoteHeight() const override {
        return remoteH_;
    }

    bool takeFrame(Frame& out) override {
        std::lock_guard<std::mutex> lock(frameMutex_);
        if (!frameDirty_) {
            return false;
        }
        out = frame_;
        frameDirty_ = false;
        return true;
    }

    bool takeCursor(CursorImage& out) override {
        std::lock_guard<std::mutex> lock(cursorMutex_);
        if (!cursorDirty_) {
            return false;
        }
        out = cursor_;
        cursorDirty_ = false;
        return true;
    }

    bool sendMouseMove(float nx, float ny) override {
        if (!connected_) {
            return false;
        }
        lastNx_ = nx;
        lastNy_ = ny;
        std::lock_guard<std::mutex> lock(sendMutex_);
        return socket_.sendAll(encodeMouseMove(nx, ny));
    }

    bool sendMouseButton(uint8_t button, bool down) override {
        if (!connected_) {
            return false;
        }
        std::lock_guard<std::mutex> lock(sendMutex_);
        return socket_.sendAll(encodeMouseButton(button, down ? 1 : 0));
    }

    bool sendMouseWheel(int32_t dx, int32_t dy) override {
        if (!connected_) {
            return false;
        }
        std::lock_guard<std::mutex> lock(sendMutex_);
        return socket_.sendAll(encodeMouseWheel(dx, dy));
    }

    bool sendKey(uint16_t macKeycode, uint32_t modifiers, bool down) override {
        if (!connected_) {
            return false;
        }
        std::lock_guard<std::mutex> lock(sendMutex_);
        return socket_.sendAll(encodeKey(macKeycode, modifiers, down ? 1 : 0));
    }

    void disconnect() override {
        if (!connected_.exchange(false)) {
            // Already disconnected; still join if needed.
        }
        socket_.close();
        if (recvThread_.joinable()) {
            if (std::this_thread::get_id() != recvThread_.get_id()) {
                recvThread_.join();
            }
        }
        decoder_.stop();
    }

private:
    void recvLoop() {
        while (connected_ && socket_.valid()) {
            auto msg = socket_.recvMessage();
            if (!msg) {
                connected_ = false;
                break;
            }
            if (msg->type == MsgType::Video) {
                decoder_.decode(msg->payload.data(), msg->payload.size());
            } else if (msg->type == MsgType::CursorImage) {
                uint32_t width = 0, height = 0, hotX = 0, hotY = 0;
                const uint8_t* pixels = nullptr;
                size_t length = 0;
                if (parseCursorImage(msg->payload, width, height, hotX, hotY, pixels, length)) {
                    std::lock_guard<std::mutex> lock(cursorMutex_);
                    cursor_.width = width;
                    cursor_.height = height;
                    cursor_.hotX = hotX;
                    cursor_.hotY = hotY;
                    cursor_.bgra.assign(pixels, pixels + length);
                    cursorDirty_ = true;
                }
            }
        }
        connected_ = false;
    }

    TcpSocket socket_;
    VtDecoder decoder_;
    std::thread recvThread_;
    std::atomic<bool> connected_{false};
    std::mutex sendMutex_;

    uint32_t remoteW_ = 0;
    uint32_t remoteH_ = 0;

    std::mutex frameMutex_;
    Frame frame_;
    bool frameDirty_ = false;

    std::mutex cursorMutex_;
    CursorImage cursor_;
    bool cursorDirty_ = false;

    float lastNx_ = 0.5f;
    float lastNy_ = 0.5f;
};

std::optional<TcpSocket> connectSocket(const ConnectOptions& options) {
    auto sock = TcpSocket::connect(options.host, options.port);
    if (!sock) {
        return std::nullopt;
    }
    sock->setRecvTimeout(options.connectTimeoutMs);
    sock->setSendTimeout(options.connectTimeoutMs);
    sock->enableKeepalive();
    return sock;
}

void setError(ConnectError* error, std::string* message, ConnectError code, const std::string& msg) {
    if (error) {
        *error = code;
    }
    if (message) {
        *message = msg;
    }
}

} // namespace

std::unique_ptr<Session> connect(const ConnectOptions& options,
                                 ConnectError* error,
                                 std::string* message) {
    if (options.host.empty() || options.managerCode.empty()) {
        setError(error, message, ConnectError::Protocol, "host and managerCode are required");
        return nullptr;
    }

    if (options.resetCounter) {
        Hotp::saveCounter(options.counterName, 0);
    }

    Hotp hotp(Hotp::deriveSecret(options.managerCode));
    auto sock = connectSocket(options);
    if (!sock) {
        setError(error, message, ConnectError::Unreachable,
                 "could not reach " + options.host + ":" + std::to_string(options.port));
        return nullptr;
    }

    bool ok = false;
    uint32_t rw = 0, rh = 0;

    for (int authAttempt = 0; authAttempt < 3; ++authAttempt) {
        const uint64_t counter = Hotp::loadCounter(options.counterName);
        if (!sock->sendAll(encodeAuth(counter, hotp.codeAt(counter)))) {
            setError(error, message, ConnectError::Protocol, "failed to send auth");
            return nullptr;
        }

        auto authResult = sock->recvMessage();
        if (!authResult || authResult->type != MsgType::AuthResult) {
            setError(error, message, ConnectError::TimedOut,
                     "worker accepted the connection but never replied");
            return nullptr;
        }

        uint64_t expectedCounter = 0;
        if (!parseAuthResult(authResult->payload, ok, rw, rh, expectedCounter)) {
            setError(error, message, ConnectError::Protocol, "invalid AuthResult");
            return nullptr;
        }
        if (ok) {
            Hotp::saveCounter(options.counterName, counter + 1);
            break;
        }

        if (expectedCounter <= counter) {
            setError(error, message, ConnectError::AuthRejected,
                     "pairing rejected — manager code does not match");
            return nullptr;
        }
        Hotp::saveCounter(options.counterName, expectedCounter);

        sock = connectSocket(options);
        if (!sock) {
            setError(error, message, ConnectError::Unreachable,
                     "lost connection while re-syncing counters");
            return nullptr;
        }
    }

    if (!ok || !sock) {
        setError(error, message, ConnectError::AuthRejected, "authentication failed");
        return nullptr;
    }

    if (error) {
        *error = ConnectError::None;
    }
    if (message) {
        message->clear();
    }

    return std::make_unique<SessionImpl>(std::move(*sock), rw ? rw : 1920, rh ? rh : 1080);
}

} // namespace openshare
