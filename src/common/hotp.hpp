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

#include <cstdint>
#include <optional>
#include <string>

namespace openshare {

class Hotp {
public:
    explicit Hotp(std::string secret);

    // Derive the rolling-code (worker) secret from the one-time manager code
    // (SHA-256, so the raw manager code never acts as the HMAC key directly).
    static std::string deriveSecret(const std::string& managerCode);

    // Generate 6-digit HOTP for counter.
    std::string codeAt(uint64_t counter) const;

    // Verify code against counter window [start, start+window).
    // On success, returns the matched counter; otherwise nullopt.
    std::optional<uint64_t> verify(const std::string& code, uint64_t start, uint64_t window = 5) const;

    // Persist / load a named counter under ~/.openshare/<name>
    static uint64_t loadCounter(const std::string& name);
    static void saveCounter(const std::string& name, uint64_t counter);
    static std::string counterPath(const std::string& name);

private:
    std::string secret_;
};

} // namespace openshare
