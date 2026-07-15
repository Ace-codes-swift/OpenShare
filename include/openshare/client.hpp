#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace openshare {

/// Latest decoded remote frame (BGRA32, tightly packed).
struct Frame {
    int width = 0;
    int height = 0;
    /// Bytes per row (= width * 4 for the packed frames we produce).
    int stride = 0;
    std::vector<uint8_t> bgra;
};

/// Remote cursor bitmap (BGRA32) with hotspot in pixel coords of that bitmap.
struct CursorImage {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t hotX = 0;
    uint32_t hotY = 0;
    std::vector<uint8_t> bgra;
};

enum class ConnectError {
    None = 0,
    Unreachable,      // TCP connect failed
    TimedOut,         // worker accepted but never replied
    AuthRejected,     // wrong manager code
    Protocol,         // malformed / unexpected response
    Internal,         // decoder / local failure
};

struct ConnectOptions {
    std::string host;
    uint16_t port = 9000;
    /// Manager code shown by OpenShareCompanion (e.g. "XXXX-XXXX-XXXX-XXXX-XXXX").
    std::string managerCode;
    /// Named HOTP counter file under ~/.openshare/ (default shared with the app).
    std::string counterName = "hotp_counter_manager";
    /// Reset the counter to 0 before connecting (use after a fresh companion code).
    bool resetCounter = true;
    int connectTimeoutMs = 10000;
};

/// Live session with a companion. Own it; destroying disconnects.
class Session {
public:
    virtual ~Session() = default;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    /// True while the TCP session is up.
    virtual bool connected() const = 0;

    /// Size advertised at auth time (window hint). Actual frames may differ.
    virtual uint32_t remoteWidth() const = 0;
    virtual uint32_t remoteHeight() const = 0;

    /// Copy the newest decoded frame if one arrived since the last call.
    /// Returns true when `out` was filled.
    virtual bool takeFrame(Frame& out) = 0;

    /// Copy the newest cursor image if one arrived since the last call.
    virtual bool takeCursor(CursorImage& out) = 0;

    /// Normalized mouse position in [0, 1] relative to the remote screen.
    virtual bool sendMouseMove(float nx, float ny) = 0;
    /// button: 0=left, 1=right, 2=middle
    virtual bool sendMouseButton(uint8_t button, bool down) = 0;
    /// Pixel-unit scroll deltas (same convention as the OpenShare viewer).
    virtual bool sendMouseWheel(int32_t dx, int32_t dy) = 0;
    /// macOS virtual keycode + protocol modifier bits (see protocol.hpp).
    virtual bool sendKey(uint16_t macKeycode, uint32_t modifiers, bool down) = 0;

    virtual void disconnect() = 0;

protected:
    Session() = default;
};

/// Connect and authenticate. On failure returns nullptr and sets `error` /
/// `message` when provided.
std::unique_ptr<Session> connect(const ConnectOptions& options,
                                 ConnectError* error = nullptr,
                                 std::string* message = nullptr);

/// Convenience: connect(host, port, managerCode).
inline std::unique_ptr<Session> connect(const std::string& host,
                                        uint16_t port,
                                        const std::string& managerCode,
                                        ConnectError* error = nullptr,
                                        std::string* message = nullptr) {
    ConnectOptions opts;
    opts.host = host;
    opts.port = port;
    opts.managerCode = managerCode;
    return connect(opts, error, message);
}

} // namespace openshare
