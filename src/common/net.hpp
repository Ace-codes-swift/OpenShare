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

#pragma once

#include "protocol.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openshare {

/// UDP transport for OpenShare (LAN / low-latency).
/// Large messages (video) are fragmented; Auth/AuthResult are retried until ACK.
/// API mirrors the old TCP helper so apps can send length-prefixed encode() buffers.
class UdpSocket {
public:
    UdpSocket() = default;
    explicit UdpSocket(int fd);
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    bool valid() const { return fd_ >= 0; }
    int fd() const { return fd_; }
    void close();

    /// Bind 0.0.0.0:port (companion).
    static std::optional<UdpSocket> listen(uint16_t port, int /*backlog*/ = 0);

    /// Create a client socket aimed at host:port (viewer).
    static std::optional<UdpSocket> connect(const std::string& host, uint16_t port);

    /// Companion: wait until a peer sends a complete message, lock onto that peer,
    /// and return a session socket (same UDP flow; first message is queued).
    /// For single-session hosts this returns a moved handle to *this logic via a
    /// dedicated session object sharing the bound port through peer lock on *this.
    /// Prefer clearPeer() + recvMessage() on the listen socket; accept() is provided
    /// for drop-in structure and returns a socket referencing the same fd (moved).
    std::optional<UdpSocket> accept();

    /// Forget the locked peer so the next packet may come from anyone (new session).
    void clearPeer();

    bool sendAll(const uint8_t* data, size_t len);
    bool sendAll(const std::vector<uint8_t>& data);

    std::optional<protocol::Message> recvMessage();

    bool waitReadable(int timeoutMs);

    void setBlocking(bool blocking);
    void setRecvTimeout(int ms);
    void setSendTimeout(int ms);

    /// No-op on UDP (kept so call sites compile).
    void enableKeepalive(int idleSec = 5, int intervalSec = 5, int count = 3);

private:
    bool sendMessage(protocol::MsgType type, const uint8_t* payload, size_t len, bool reliable);
    bool sendRawPacket(const uint8_t* data, size_t len);
    void pumpIncoming(int waitMs);

    int fd_ = -1;
    bool ownsFd_ = true;

    bool havePeer_ = false;
    uint8_t peerAddr_[128]{};
    uint32_t peerAddrLen_ = 0;

    int recvTimeoutMs_ = 0;
    uint32_t nextMsgId_ = 1;
    uint32_t lastAckMsgId_ = 0;

    struct Assembler {
        uint32_t msgId = 0;
        protocol::MsgType type{};
        uint16_t fragCount = 0;
        std::vector<std::vector<uint8_t>> frags;
        std::vector<uint8_t> have;
        size_t got = 0;
        std::chrono::steady_clock::time_point started{};
    };
    Assembler asm_{};
    std::vector<protocol::Message> inbox_;
};

// Back-compat alias while call sites migrate.
using TcpSocket = UdpSocket;

} // namespace openshare
