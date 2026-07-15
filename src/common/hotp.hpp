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
