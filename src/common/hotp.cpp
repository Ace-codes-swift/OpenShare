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

#include "hotp.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace openshare {

Hotp::Hotp(std::string secret) : secret_(std::move(secret)) {}

std::string Hotp::deriveSecret(const std::string& managerCode) {
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return {};
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, managerCode.data(), managerCode.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &digestLen) != 1) {
        EVP_MD_CTX_free(ctx);
        return {};
    }
    EVP_MD_CTX_free(ctx);
    return std::string(reinterpret_cast<const char*>(digest), digestLen);
}

std::string Hotp::codeAt(uint64_t counter) const {
    uint8_t msg[8];
    for (int i = 7; i >= 0; --i) {
        msg[i] = static_cast<uint8_t>(counter & 0xff);
        counter >>= 8;
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;
    if (HMAC(EVP_sha1(),
             secret_.data(),
             static_cast<int>(secret_.size()),
             msg,
             sizeof(msg),
             digest,
             &digestLen) == nullptr ||
        digestLen < 20) {
        return "000000";
    }

    const int offset = digest[digestLen - 1] & 0x0f;
    const uint32_t binary =
        ((digest[offset] & 0x7f) << 24) | ((digest[offset + 1] & 0xff) << 16) |
        ((digest[offset + 2] & 0xff) << 8) | (digest[offset + 3] & 0xff);

    const uint32_t otp = binary % 1000000;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06u", otp);
    return buf;
}

std::optional<uint64_t> Hotp::verify(const std::string& code, uint64_t start, uint64_t window) const {
    std::string normalized;
    for (char c : code) {
        if (c >= '0' && c <= '9') {
            normalized.push_back(c);
        }
    }
    while (normalized.size() < 6) {
        normalized = "0" + normalized;
    }
    if (normalized.size() > 6) {
        normalized = normalized.substr(normalized.size() - 6);
    }

    for (uint64_t i = 0; i < window; ++i) {
        if (codeAt(start + i) == normalized) {
            return start + i;
        }
    }
    return std::nullopt;
}

std::string Hotp::counterPath(const std::string& name) {
    const char* home = std::getenv("HOME");
    if (!home) {
        return "/tmp/openshare_" + name;
    }
    return std::string(home) + "/.openshare/" + name;
}

uint64_t Hotp::loadCounter(const std::string& name) {
    std::ifstream in(counterPath(name));
    if (!in) {
        return 0;
    }
    uint64_t v = 0;
    in >> v;
    return v;
}

void Hotp::saveCounter(const std::string& name, uint64_t counter) {
    const char* home = std::getenv("HOME");
    if (home) {
        std::string dir = std::string(home) + "/.openshare";
        mkdir(dir.c_str(), 0700);
    }
    std::ofstream out(counterPath(name), std::ios::trunc);
    out << counter << "\n";
}

} // namespace openshare
