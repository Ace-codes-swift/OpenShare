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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openshare {

class TcpSocket {
public:
    TcpSocket() = default;
    explicit TcpSocket(int fd);
    ~TcpSocket();

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    bool valid() const { return fd_ >= 0; }
    int fd() const { return fd_; }
    void close();

    static std::optional<TcpSocket> listen(uint16_t port, int backlog = 4);
    static std::optional<TcpSocket> connect(const std::string& host, uint16_t port);

    std::optional<TcpSocket> accept();

    bool sendAll(const uint8_t* data, size_t len);
    bool sendAll(const std::vector<uint8_t>& data);

    // Blocking read of one protocol message. Returns nullopt on disconnect/error.
    std::optional<protocol::Message> recvMessage();

    // Non-blocking poll: true if data may be readable.
    bool waitReadable(int timeoutMs);

    void setBlocking(bool blocking);

    // 0 disables the timeout (blocking forever). A timed-out recv/send is
    // reported as a failure by recvMessage/sendAll.
    void setRecvTimeout(int ms);
    void setSendTimeout(int ms);

    // Detect silently-dead peers (sleep, crash, network drop) instead of
    // blocking on them forever.
    void enableKeepalive(int idleSec = 5, int intervalSec = 5, int count = 3);

private:
    bool recvExact(uint8_t* buf, size_t len);

    int fd_ = -1;
};

} // namespace openshare
