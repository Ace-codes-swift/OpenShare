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

#include "pairing.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <vector>

namespace openshare {
namespace {

constexpr const char* kWorkerCodeFile = "worker-code";
constexpr const char* kManagerCodeFile = "manager-code";
constexpr const char* kWorkerHostFile = "worker_host";

std::string openshareDir() {
    const char* home = std::getenv("HOME");
    if (!home) {
        return "/tmp";
    }
    return std::string(home) + "/.openshare";
}

std::string filePath(const char* name) {
    return openshareDir() + "/" + name;
}

bool ensureDir() {
    const std::string dir = openshareDir();
    if (dir == "/tmp") {
        return true;
    }
    return mkdir(dir.c_str(), 0700) == 0 || errno == EEXIST;
}

std::optional<std::string> readFile(const char* name) {
    std::ifstream in(filePath(name));
    if (!in) {
        return std::nullopt;
    }
    std::string value;
    std::getline(in, value);
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

bool writeFile(const char* name, const std::string& value) {
    if (!ensureDir()) {
        return false;
    }
    std::ofstream out(filePath(name), std::ios::trunc);
    if (!out) {
        return false;
    }
    out << value << "\n";
    return static_cast<bool>(out);
}

std::string normalizeCode(std::string code) {
    std::string out;
    out.reserve(code.size());
    for (char c : code) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string generateCode() {
    constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::array<uint8_t, 20> random{};
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (!urandom.read(reinterpret_cast<char*>(random.data()), random.size())) {
        return {};
    }

    std::string code;
    code.reserve(24);
    for (size_t i = 0; i < random.size(); ++i) {
        if (i > 0 && i % 4 == 0) {
            code.push_back('-');
        }
        code.push_back(alphabet[random[i] % (sizeof(alphabet) - 1)]);
    }
    return code;
}

bool haveZenity() {
    return std::system("command -v zenity >/dev/null 2>&1") == 0;
}

std::vector<std::string> localIPv4Addresses() {
    std::vector<std::string> addrs;
    ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) {
        return addrs;
    }
    for (ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK)) {
            continue;
        }
        char buf[INET_ADDRSTRLEN] = {0};
        auto* sin = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) {
            addrs.emplace_back(buf);
        }
    }
    freeifaddrs(ifaddr);
    return addrs;
}

std::string formatAddressLine() {
    std::string addrLine = "Address: ";
    const auto ips = localIPv4Addresses();
    if (ips.empty()) {
        addrLine += "(no network address detected)";
    } else {
        for (size_t i = 0; i < ips.size(); ++i) {
            if (i > 0) {
                addrLine += ", ";
            }
            addrLine += ips[i];
        }
    }
    return addrLine;
}

std::optional<std::pair<std::string, std::string>> promptManagerPairingCli(
    const std::string& prefillHost, const std::string& prefillCode) {
    std::cout << "\nPair with an OpenShare worker\n";
    std::cout << "Enter the worker's address and the manager code shown by OpenShareCompanion.\n\n";

    std::string host = prefillHost;
    if (host.empty() || host == "127.0.0.1") {
        host.clear();
    }
    std::cout << "Worker address";
    if (!host.empty()) {
        std::cout << " [" << host << "]";
    }
    std::cout << ": ";
    std::string hostInput;
    std::getline(std::cin, hostInput);
    if (!hostInput.empty()) {
        host = hostInput;
    }

    std::string code = prefillCode;
    std::cout << "Manager code";
    if (!code.empty()) {
        std::cout << " [" << code << "]";
    }
    std::cout << ": ";
    std::string codeInput;
    std::getline(std::cin, codeInput);
    if (!codeInput.empty()) {
        code = codeInput;
    }

    code = normalizeCode(code);
    while (!host.empty() && (host.front() == ' ' || host.back() == ' ')) {
        if (host.front() == ' ') {
            host.erase(host.begin());
        }
        if (!host.empty() && host.back() == ' ') {
            host.pop_back();
        }
    }

    if (host.empty() || code.size() < 20) {
        std::cerr << "Incomplete pairing details — enter both address and full manager code.\n";
        return std::nullopt;
    }
    return std::make_pair(host, code);
}

std::optional<std::pair<std::string, std::string>> promptManagerPairing(
    const std::string& prefillHost, const std::string& prefillCode) {
    if (!haveZenity()) {
        return promptManagerPairingCli(prefillHost, prefillCode);
    }

    std::string hostArg = prefillHost;
    if (hostArg == "127.0.0.1") {
        hostArg.clear();
    }

    std::ostringstream cmd;
    cmd << "zenity --forms --title='Pair with OpenShare worker'"
        << " --text='Enter the worker address and manager code from OpenShareCompanion.'"
        << " --add-entry='Worker address' --entry-text='" << hostArg << "'"
        << " --add-entry='Manager code' --entry-text='" << prefillCode << "'"
        << " 2>/dev/null";

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        return promptManagerPairingCli(prefillHost, prefillCode);
    }

    char buffer[512] = {0};
    std::string line;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        line += buffer;
    }
    const int status = pclose(pipe);
    if (status != 0 || line.empty()) {
        return std::nullopt;
    }

    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }

    const auto sep = line.find('|');
    if (sep == std::string::npos) {
        return promptManagerPairingCli(prefillHost, prefillCode);
    }

    std::string host = line.substr(0, sep);
    std::string code = normalizeCode(line.substr(sep + 1));
    while (!host.empty() && (host.front() == ' ' || host.back() == ' ')) {
        if (host.front() == ' ') {
            host.erase(host.begin());
        }
        if (!host.empty() && host.back() == ' ') {
            host.pop_back();
        }
    }

    if (host.empty() || code.size() < 20) {
        showPairingError("Enter both the worker address and the complete manager code.");
        return std::nullopt;
    }
    return std::make_pair(host, code);
}

} // namespace

std::optional<PairingCode> loadOrCreateWorkerPairingCode() {
    if (auto existing = readFile(kWorkerCodeFile)) {
        return PairingCode{*existing, false};
    }

    std::string code = generateCode();
    if (code.empty() || !writeFile(kWorkerCodeFile, code)) {
        return std::nullopt;
    }
    return PairingCode{std::move(code), true};
}

std::optional<std::string> loadWorkerPairingCode() {
    return readFile(kWorkerCodeFile);
}

std::optional<std::string> regenerateWorkerPairingCode() {
    std::string code = generateCode();
    if (code.empty() || !writeFile(kWorkerCodeFile, code)) {
        return std::nullopt;
    }
    return code;
}

void showWorkerReadyDialog(const std::string& code) {
    const std::string addrLine = formatAddressLine();
    const std::string message =
        "The worker is already listening. On your other machine, open OpenShare and "
        "enter this machine address and the manager code below.\n\n" +
        addrLine + "\n\nManager code:\n" + code +
        "\n\nClose this dialog when you have copied the code. The worker keeps running.";

    if (haveZenity()) {
        const std::string textFile = filePath(".zenity-worker-ready.txt");
        {
            std::ofstream out(textFile);
            out << message;
        }
        std::ostringstream cmd;
        cmd << "zenity --info --title='OpenShare worker ready' --width=480"
            << " --textfile='" << textFile << "' 2>/dev/null";
        if (std::system(cmd.str().c_str()) == 0) {
            std::remove(textFile.c_str());
            return;
        }
        std::remove(textFile.c_str());
    }

    std::cout << "\n=== OpenShare worker is ready to pair ===\n";
    std::cout << addrLine << "\n";
    std::cout << "Manager code: " << code << "\n";
    std::cout << "Press Enter when you've copied the code...\n";
    std::string line;
    std::getline(std::cin, line);
}

std::optional<ManagerPairing> loadManagerPairing() {
    auto code = readFile(kManagerCodeFile);
    if (!code) {
        return std::nullopt;
    }
    const std::string host = readFile(kWorkerHostFile).value_or("");
    return ManagerPairing{*code, host, false};
}

std::optional<ManagerPairing> loadOrPromptManagerPairing() {
    if (auto existing = loadManagerPairing()) {
        return existing;
    }

    auto entered = promptManagerPairing("", "");
    if (!entered) {
        return std::nullopt;
    }
    const auto& [host, code] = *entered;
    if (!writeFile(kManagerCodeFile, code) || !writeFile(kWorkerHostFile, host)) {
        return std::nullopt;
    }
    return ManagerPairing{code, host, true};
}

std::optional<ManagerPairing> repromptManagerPairing(const std::string& prefillHost,
                                                     const std::string& prefillCode) {
    auto entered = promptManagerPairing(prefillHost, prefillCode);
    if (!entered) {
        return std::nullopt;
    }
    const auto& [host, code] = *entered;
    if (!writeFile(kManagerCodeFile, code) || !writeFile(kWorkerHostFile, host)) {
        return std::nullopt;
    }
    return ManagerPairing{code, host, true};
}

void showPairingError(const std::string& message) {
    if (haveZenity()) {
        const std::string textFile = filePath(".zenity-error.txt");
        {
            std::ofstream out(textFile);
            out << message;
        }
        std::ostringstream cmd;
        cmd << "zenity --error --title='OpenShare could not connect' --textfile='"
            << textFile << "' 2>/dev/null";
        if (std::system(cmd.str().c_str()) == 0) {
            std::remove(textFile.c_str());
            return;
        }
        std::remove(textFile.c_str());
    }
    std::cerr << "\nOpenShare could not connect:\n" << message << "\n";
}

} // namespace openshare
