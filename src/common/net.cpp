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

#include "net.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace openshare {
namespace {

constexpr uint32_t kUdpMagic = 0x4F535544; // 'OSUD'
constexpr uint8_t kUdpVersion = 2;
constexpr size_t kUdpHeaderSize = 16;
constexpr size_t kMaxPayloadPerPacket = 1200;
constexpr size_t kMaxDatagram = kUdpHeaderSize + kMaxPayloadPerPacket;
constexpr size_t kMaxMessageBytes = 8 * 1024 * 1024;
constexpr uint8_t kFlagReliable = 1u << 0;
constexpr uint8_t kFlagAck = 1u << 1;

void putU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xff);
    p[1] = static_cast<uint8_t>(v & 0xff);
}

uint16_t getU16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

void putU32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xff);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xff);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xff);
    p[3] = static_cast<uint8_t>(v & 0xff);
}

uint32_t getU32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

bool addrsEqual(const void* a, uint32_t aLen, const sockaddr* b, socklen_t bLen) {
    return aLen == static_cast<uint32_t>(bLen) && std::memcmp(a, b, aLen) == 0;
}

bool reliableType(protocol::MsgType t) {
    return t == protocol::MsgType::Auth || t == protocol::MsgType::AuthResult;
}

} // namespace

UdpSocket::UdpSocket(int fd) : fd_(fd) {}

UdpSocket::~UdpSocket() {
    close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept {
    *this = std::move(other);
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        ownsFd_ = other.ownsFd_;
        havePeer_ = other.havePeer_;
        peerAddrLen_ = other.peerAddrLen_;
        std::memcpy(peerAddr_, other.peerAddr_, sizeof(peerAddr_));
        recvTimeoutMs_ = other.recvTimeoutMs_;
        nextMsgId_ = other.nextMsgId_;
        lastAckMsgId_ = other.lastAckMsgId_;
        asm_ = std::move(other.asm_);
        inbox_ = std::move(other.inbox_);
        other.fd_ = -1;
        other.ownsFd_ = true;
        other.havePeer_ = false;
        other.peerAddrLen_ = 0;
        other.lastAckMsgId_ = 0;
    }
    return *this;
}

void UdpSocket::close() {
    if (fd_ >= 0 && ownsFd_) {
        ::close(fd_);
    }
    fd_ = -1;
    havePeer_ = false;
    peerAddrLen_ = 0;
    inbox_.clear();
    asm_ = {};
}

void UdpSocket::clearPeer() {
    havePeer_ = false;
    peerAddrLen_ = 0;
    inbox_.clear();
    asm_ = {};
    lastAckMsgId_ = 0;
}

void UdpSocket::setBlocking(bool blocking) {
    if (fd_ < 0) {
        return;
    }
    const int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0) {
        return;
    }
    fcntl(fd_, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
}

void UdpSocket::setRecvTimeout(int ms) {
    recvTimeoutMs_ = ms;
}

void UdpSocket::setSendTimeout(int /*ms*/) {}

void UdpSocket::enableKeepalive(int, int, int) {}

std::optional<UdpSocket> UdpSocket::listen(uint16_t port, int) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return std::nullopt;
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    int buf = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return std::nullopt;
    }
    return UdpSocket(fd);
}

std::optional<UdpSocket> UdpSocket::connect(const std::string& host, uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        return std::nullopt;
    }

    const int fd = ::socket(res->ai_family, SOCK_DGRAM, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        return std::nullopt;
    }
    int buf = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        ::close(fd);
        freeaddrinfo(res);
        return std::nullopt;
    }

    UdpSocket sock(fd);
    if (res->ai_addrlen > sizeof(sock.peerAddr_)) {
        freeaddrinfo(res);
        return std::nullopt;
    }
    std::memcpy(sock.peerAddr_, res->ai_addr, res->ai_addrlen);
    sock.peerAddrLen_ = static_cast<uint32_t>(res->ai_addrlen);
    sock.havePeer_ = true;
    freeaddrinfo(res);
    return sock;
}

std::optional<UdpSocket> UdpSocket::accept() {
    if (fd_ < 0) {
        return std::nullopt;
    }
    clearPeer();
    while (inbox_.empty() && fd_ >= 0) {
        pumpIncoming(1000);
    }
    if (inbox_.empty() || !havePeer_) {
        return std::nullopt;
    }

    UdpSocket session;
    session.fd_ = fd_;
    session.ownsFd_ = false;
    session.havePeer_ = true;
    session.peerAddrLen_ = peerAddrLen_;
    std::memcpy(session.peerAddr_, peerAddr_, peerAddrLen_);
    session.recvTimeoutMs_ = recvTimeoutMs_;
    session.nextMsgId_ = nextMsgId_;
    session.inbox_ = std::move(inbox_);
    session.asm_ = std::move(asm_);
    inbox_.clear();
    asm_ = {};
    return session;
}

bool UdpSocket::waitReadable(int timeoutMs) {
    if (fd_ < 0) {
        return false;
    }
    if (!inbox_.empty()) {
        return true;
    }
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return select(fd_ + 1, &fds, nullptr, nullptr, &tv) > 0;
}

void UdpSocket::pumpIncoming(int waitMs) {
    if (fd_ < 0) {
        return;
    }
    // Drop half-assembled frames that never finished (lost UDP fragments).
    if (asm_.fragCount != 0) {
        const auto age = std::chrono::steady_clock::now() - asm_.started;
        if (age > std::chrono::milliseconds(250)) {
            asm_ = {};
        }
    }
    if (waitMs > 0 && inbox_.empty()) {
        waitReadable(waitMs);
    }

    while (true) {
        uint8_t buf[kMaxDatagram];
        sockaddr_storage from{};
        socklen_t fromLen = sizeof(from);

        // Non-blocking drain.
        const int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        const ssize_t n =
            ::recvfrom(fd_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        fcntl(fd_, F_SETFL, flags);

        if (n < 0) {
            break;
        }
        if (n < static_cast<ssize_t>(kUdpHeaderSize)) {
            continue;
        }
        if (getU32(buf) != kUdpMagic || buf[4] != kUdpVersion) {
            continue;
        }

        if (havePeer_) {
            if (!addrsEqual(peerAddr_, peerAddrLen_, reinterpret_cast<sockaddr*>(&from), fromLen)) {
                continue; // ignore other hosts during a session
            }
        } else {
            if (fromLen > sizeof(peerAddr_)) {
                continue;
            }
            std::memcpy(peerAddr_, &from, fromLen);
            peerAddrLen_ = static_cast<uint32_t>(fromLen);
            havePeer_ = true;
        }

        const uint8_t packetFlags = buf[5];
        const auto type = static_cast<protocol::MsgType>(buf[6]);
        const uint32_t msgId = getU32(buf + 8);
        const uint16_t fragIndex = getU16(buf + 12);
        const uint16_t fragCount = getU16(buf + 14);
        const size_t payloadLen = static_cast<size_t>(n) - kUdpHeaderSize;

        if (packetFlags & kFlagAck) {
            lastAckMsgId_ = msgId;
            continue;
        }
        if (fragCount == 0 || fragIndex >= fragCount || payloadLen > kMaxPayloadPerPacket) {
            continue;
        }

        // ACK reliable data packets.
        if (packetFlags & kFlagReliable) {
            uint8_t ack[kUdpHeaderSize];
            putU32(ack, kUdpMagic);
            ack[4] = kUdpVersion;
            ack[5] = kFlagAck;
            ack[6] = static_cast<uint8_t>(protocol::MsgType::Ack);
            ack[7] = 0;
            putU32(ack + 8, msgId);
            putU16(ack + 12, 0);
            putU16(ack + 14, 1);
            sendRawPacket(ack, kUdpHeaderSize);
        }

        // Drop incomplete older assemblies when a newer message starts
        // (especially video — prefer latest frame).
        if (asm_.fragCount != 0 && msgId != asm_.msgId) {
            const bool preferNew =
                type == protocol::MsgType::Video || msgId > asm_.msgId || asm_.got == 0;
            if (preferNew) {
                asm_ = {};
            } else {
                continue;
            }
        }

        if (asm_.fragCount == 0) {
            asm_.msgId = msgId;
            asm_.type = type;
            asm_.fragCount = fragCount;
            asm_.frags.assign(fragCount, {});
            asm_.have.assign(fragCount, 0);
            asm_.got = 0;
            asm_.started = std::chrono::steady_clock::now();
        }

        if (!asm_.have[fragIndex]) {
            asm_.frags[fragIndex].assign(buf + kUdpHeaderSize, buf + kUdpHeaderSize + payloadLen);
            asm_.have[fragIndex] = 1;
            ++asm_.got;
        }

        if (asm_.got == asm_.fragCount) {
            protocol::Message msg;
            msg.type = asm_.type;
            size_t total = 0;
            for (const auto& f : asm_.frags) {
                total += f.size();
            }
            if (total <= kMaxMessageBytes) {
                msg.payload.reserve(total);
                for (const auto& f : asm_.frags) {
                    msg.payload.insert(msg.payload.end(), f.begin(), f.end());
                }
                inbox_.push_back(std::move(msg));
            }
            asm_ = {};
        }
    }
}

bool UdpSocket::sendRawPacket(const uint8_t* data, size_t len) {
    if (fd_ < 0 || !havePeer_) {
        return false;
    }
    const ssize_t n = ::sendto(fd_,
                               data,
                               len,
                               0,
                               reinterpret_cast<const sockaddr*>(peerAddr_),
                               static_cast<socklen_t>(peerAddrLen_));
    return n == static_cast<ssize_t>(len);
}

bool UdpSocket::sendMessage(protocol::MsgType type,
                            const uint8_t* payload,
                            size_t len,
                            bool reliable) {
    if (len > kMaxMessageBytes || !havePeer_) {
        return false;
    }
    const uint32_t msgId = nextMsgId_++;
    const uint16_t fragCount = static_cast<uint16_t>(
        std::max<size_t>(1, (len + kMaxPayloadPerPacket - 1) / kMaxPayloadPerPacket));

    auto sendOnce = [&]() -> bool {
        size_t offset = 0;
        for (uint16_t i = 0; i < fragCount; ++i) {
            const size_t chunk = (offset < len) ? std::min(kMaxPayloadPerPacket, len - offset) : 0;
            uint8_t packet[kMaxDatagram];
            putU32(packet, kUdpMagic);
            packet[4] = kUdpVersion;
            packet[5] = reliable ? kFlagReliable : 0;
            packet[6] = static_cast<uint8_t>(type);
            packet[7] = 0;
            putU32(packet + 8, msgId);
            putU16(packet + 12, i);
            putU16(packet + 14, fragCount);
            if (chunk) {
                std::memcpy(packet + kUdpHeaderSize, payload + offset, chunk);
            }
            if (!sendRawPacket(packet, kUdpHeaderSize + chunk)) {
                return false;
            }
            offset += chunk;
        }
        return true;
    };

    if (!reliable) {
        return sendOnce();
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!sendOnce()) {
            return false;
        }
        const auto ackDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        while (std::chrono::steady_clock::now() < ackDeadline) {
            pumpIncoming(20);
            if (lastAckMsgId_ == msgId) {
                return true;
            }
        }
    }
    return false;
}

bool UdpSocket::sendAll(const uint8_t* data, size_t len) {
    // Accept legacy TCP frame: [u32be bodyLen][u8 type][payload...]
    if (len < 5) {
        return false;
    }
    const uint32_t bodyLen = getU32(data);
    if (bodyLen + 4 != len || bodyLen == 0) {
        return false;
    }
    const auto type = static_cast<protocol::MsgType>(data[4]);
    const uint8_t* payload = data + 5;
    const size_t payloadLen = bodyLen - 1;
    return sendMessage(type, payload, payloadLen, reliableType(type));
}

bool UdpSocket::sendAll(const std::vector<uint8_t>& data) {
    return sendAll(data.data(), data.size());
}

std::optional<protocol::Message> UdpSocket::recvMessage() {
    if (fd_ < 0) {
        return std::nullopt;
    }

    if (recvTimeoutMs_ == 0) {
        // Block until a full message is available.
        while (inbox_.empty() && fd_ >= 0) {
            pumpIncoming(1000);
        }
    } else {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(recvTimeoutMs_);
        while (inbox_.empty() && fd_ >= 0 && std::chrono::steady_clock::now() < deadline) {
            const int remain = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                    deadline - std::chrono::steady_clock::now())
                                                    .count());
            pumpIncoming(std::max(1, remain));
        }
        if (inbox_.empty()) {
            return std::nullopt;
        }
    }

    if (inbox_.empty()) {
        return std::nullopt;
    }
    protocol::Message msg = std::move(inbox_.front());
    inbox_.erase(inbox_.begin());
    return msg;
}

} // namespace openshare
