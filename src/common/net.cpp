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
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace openshare {

TcpSocket::TcpSocket(int fd) : fd_(fd) {}

TcpSocket::~TcpSocket() {
    close();
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void TcpSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void TcpSocket::setBlocking(bool blocking) {
    if (fd_ < 0) {
        return;
    }
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0) {
        return;
    }
    if (blocking) {
        fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);
    } else {
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    }
}

void TcpSocket::setRecvTimeout(int ms) {
    if (fd_ < 0) {
        return;
    }
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

void TcpSocket::setSendTimeout(int ms) {
    if (fd_ < 0) {
        return;
    }
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

void TcpSocket::enableKeepalive(int idleSec, int intervalSec, int count) {
    if (fd_ < 0) {
        return;
    }
    int yes = 1;
    setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
    setsockopt(fd_, IPPROTO_TCP, TCP_KEEPALIVE, &idleSec, sizeof(idleSec));
    setsockopt(fd_, IPPROTO_TCP, TCP_KEEPINTVL, &intervalSec, sizeof(intervalSec));
    setsockopt(fd_, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
}

std::optional<TcpSocket> TcpSocket::listen(uint16_t port, int backlog) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return std::nullopt;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return std::nullopt;
    }
    if (::listen(fd, backlog) < 0) {
        ::close(fd);
        return std::nullopt;
    }
    return TcpSocket(fd);
}

std::optional<TcpSocket> TcpSocket::connect(const std::string& host, uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    const std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0) {
        return std::nullopt;
    }

    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        return std::nullopt;
    }

    // Disable Nagle for lower input latency
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    return TcpSocket(fd);
}

std::optional<TcpSocket> TcpSocket::accept() {
    if (fd_ < 0) {
        return std::nullopt;
    }
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    int client = ::accept(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    if (client < 0) {
        return std::nullopt;
    }
    int yes = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    return TcpSocket(client);
}

bool TcpSocket::sendAll(const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::send(fd_, data + sent, len - sent, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool TcpSocket::sendAll(const std::vector<uint8_t>& data) {
    return sendAll(data.data(), data.size());
}

bool TcpSocket::recvExact(uint8_t* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        const ssize_t n = ::recv(fd_, buf + got, len - got, 0);
        if (n == 0) {
            return false;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        got += static_cast<size_t>(n);
    }
    return true;
}

std::optional<protocol::Message> TcpSocket::recvMessage() {
    uint8_t header[4];
    if (!recvExact(header, 4)) {
        return std::nullopt;
    }
    const uint32_t bodyLen = protocol::readU32BE(header);
    if (bodyLen == 0 || bodyLen > 32 * 1024 * 1024) {
        return std::nullopt;
    }

    std::vector<uint8_t> body(bodyLen);
    if (!recvExact(body.data(), bodyLen)) {
        return std::nullopt;
    }

    protocol::Message msg;
    msg.type = static_cast<protocol::MsgType>(body[0]);
    msg.payload.assign(body.begin() + 1, body.end());
    return msg;
}

bool TcpSocket::waitReadable(int timeoutMs) {
    if (fd_ < 0) {
        return false;
    }
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    const int r = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
    return r > 0 && FD_ISSET(fd_, &fds);
}

} // namespace openshare
