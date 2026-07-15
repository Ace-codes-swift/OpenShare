#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace openshare {
namespace protocol {

inline constexpr uint8_t kVersion = 1;

enum class MsgType : uint8_t {
    Auth = 1,
    AuthResult = 2,
    Video = 3,
    MouseMove = 4,
    MouseButton = 5,
    MouseWheel = 6,
    Key = 7,
    CursorImage = 8,
};

struct Message {
    MsgType type{};
    std::vector<uint8_t> payload;
};

inline void writeU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(v & 0xff));
}

inline uint32_t readU32BE(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline void writeF32BE(std::vector<uint8_t>& out, float f) {
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    writeU32BE(out, bits);
}

inline float readF32BE(const uint8_t* p) {
    uint32_t bits = readU32BE(p);
    float f = 0;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

inline void writeI32BE(std::vector<uint8_t>& out, int32_t v) {
    writeU32BE(out, static_cast<uint32_t>(v));
}

inline int32_t readI32BE(const uint8_t* p) {
    return static_cast<int32_t>(readU32BE(p));
}

// Wire format: [uint32_be total_length][uint8 type][payload...]
// total_length = 1 + payload.size()
inline std::vector<uint8_t> encode(MsgType type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    const uint32_t bodyLen = 1 + static_cast<uint32_t>(payload.size());
    writeU32BE(out, bodyLen);
    out.push_back(static_cast<uint8_t>(type));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

inline void writeU64BE(std::vector<uint8_t>& out, uint64_t v) {
    writeU32BE(out, static_cast<uint32_t>(v >> 32));
    writeU32BE(out, static_cast<uint32_t>(v & 0xffffffff));
}

inline uint64_t readU64BE(const uint8_t* p) {
    return (uint64_t(readU32BE(p)) << 32) | readU32BE(p + 4);
}

// Auth carries the viewer's rolling counter plus the HOTP code at that
// counter, so the host can verify and resync (car-key style).
inline std::vector<uint8_t> encodeAuth(uint64_t counter, const std::string& code) {
    std::vector<uint8_t> payload;
    payload.push_back(kVersion);
    writeU64BE(payload, counter);
    // 6-digit ASCII, left-padded
    std::string padded = code;
    while (padded.size() < 6) {
        padded = "0" + padded;
    }
    if (padded.size() > 6) {
        padded = padded.substr(padded.size() - 6);
    }
    payload.insert(payload.end(), padded.begin(), padded.end());
    return encode(MsgType::Auth, payload);
}

inline bool parseAuth(const std::vector<uint8_t>& payload, uint8_t& version, uint64_t& counter,
                      std::string& code) {
    if (payload.size() < 15) {
        return false;
    }
    version = payload[0];
    counter = readU64BE(payload.data() + 1);
    code.assign(reinterpret_cast<const char*>(payload.data() + 9), 6);
    return true;
}

// On failure, expectedCounter carries the host's next acceptable counter so
// the viewer can resync and retry (like a car key re-syncing with the car).
// Knowing the counter is useless without the secret, so this leaks nothing.
inline std::vector<uint8_t> encodeAuthResult(bool ok, uint32_t width, uint32_t height,
                                             uint64_t expectedCounter = 0) {
    std::vector<uint8_t> payload;
    payload.push_back(ok ? 1 : 0);
    writeU32BE(payload, width);
    writeU32BE(payload, height);
    writeU64BE(payload, expectedCounter);
    return encode(MsgType::AuthResult, payload);
}

inline bool parseAuthResult(const std::vector<uint8_t>& payload, bool& ok, uint32_t& width,
                            uint32_t& height, uint64_t& expectedCounter) {
    if (payload.size() < 9) {
        return false;
    }
    ok = payload[0] != 0;
    width = readU32BE(payload.data() + 1);
    height = readU32BE(payload.data() + 5);
    expectedCounter = payload.size() >= 17 ? readU64BE(payload.data() + 9) : 0;
    return true;
}

inline std::vector<uint8_t> encodeVideo(const uint8_t* data, size_t len) {
    std::vector<uint8_t> payload(data, data + len);
    return encode(MsgType::Video, payload);
}

inline std::vector<uint8_t> encodeMouseMove(float x, float y) {
    std::vector<uint8_t> payload;
    writeF32BE(payload, x);
    writeF32BE(payload, y);
    return encode(MsgType::MouseMove, payload);
}

inline bool parseMouseMove(const std::vector<uint8_t>& payload, float& x, float& y) {
    if (payload.size() < 8) {
        return false;
    }
    x = readF32BE(payload.data());
    y = readF32BE(payload.data() + 4);
    return true;
}

// button: 0=left, 1=right, 2=middle; down: 1=down, 0=up
inline std::vector<uint8_t> encodeMouseButton(uint8_t button, uint8_t down) {
    std::vector<uint8_t> payload{button, down};
    return encode(MsgType::MouseButton, payload);
}

inline bool parseMouseButton(const std::vector<uint8_t>& payload, uint8_t& button, uint8_t& down) {
    if (payload.size() < 2) {
        return false;
    }
    button = payload[0];
    down = payload[1];
    return true;
}

inline std::vector<uint8_t> encodeMouseWheel(int32_t dx, int32_t dy) {
    std::vector<uint8_t> payload;
    writeI32BE(payload, dx);
    writeI32BE(payload, dy);
    return encode(MsgType::MouseWheel, payload);
}

inline bool parseMouseWheel(const std::vector<uint8_t>& payload, int32_t& dx, int32_t& dy) {
    if (payload.size() < 8) {
        return false;
    }
    dx = readI32BE(payload.data());
    dy = readI32BE(payload.data() + 4);
    return true;
}

// keycode: mac virtual key code; modifiers bitfield; down: 1/0
inline std::vector<uint8_t> encodeKey(uint16_t keycode, uint32_t modifiers, uint8_t down) {
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>((keycode >> 8) & 0xff));
    payload.push_back(static_cast<uint8_t>(keycode & 0xff));
    writeU32BE(payload, modifiers);
    payload.push_back(down);
    return encode(MsgType::Key, payload);
}

inline bool parseKey(const std::vector<uint8_t>& payload, uint16_t& keycode, uint32_t& modifiers, uint8_t& down) {
    if (payload.size() < 7) {
        return false;
    }
    keycode = static_cast<uint16_t>((payload[0] << 8) | payload[1]);
    modifiers = readU32BE(payload.data() + 2);
    down = payload[6];
    return true;
}

// Cursor bitmap is BGRA32. Header: width, height, hotspot x/y (all u32 BE).
inline std::vector<uint8_t> encodeCursorImage(uint32_t width,
                                              uint32_t height,
                                              uint32_t hotX,
                                              uint32_t hotY,
                                              const uint8_t* bgra,
                                              size_t len) {
    std::vector<uint8_t> payload;
    payload.reserve(16 + len);
    writeU32BE(payload, width);
    writeU32BE(payload, height);
    writeU32BE(payload, hotX);
    writeU32BE(payload, hotY);
    payload.insert(payload.end(), bgra, bgra + len);
    return encode(MsgType::CursorImage, payload);
}

inline bool parseCursorImage(const std::vector<uint8_t>& payload,
                             uint32_t& width,
                             uint32_t& height,
                             uint32_t& hotX,
                             uint32_t& hotY,
                             const uint8_t*& bgra,
                             size_t& len) {
    if (payload.size() < 16) {
        return false;
    }
    width = readU32BE(payload.data());
    height = readU32BE(payload.data() + 4);
    hotX = readU32BE(payload.data() + 8);
    hotY = readU32BE(payload.data() + 12);
    len = payload.size() - 16;
    bgra = payload.data() + 16;
    return width > 0 && height > 0 && width <= 512 && height <= 512 &&
           len == static_cast<size_t>(width) * height * 4 &&
           hotX < width && hotY < height;
}

// Modifier bits (match CGEventFlags low bits subset)
inline constexpr uint32_t kModShift = 1u << 0;
inline constexpr uint32_t kModCtrl = 1u << 1;
inline constexpr uint32_t kModAlt = 1u << 2;
inline constexpr uint32_t kModCmd = 1u << 3;

} // namespace protocol
} // namespace openshare
