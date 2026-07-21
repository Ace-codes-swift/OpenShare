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

#include <optional>
#include <string>

namespace openshare {

struct PairingCode {
    std::string value;
    bool created = false;
};

struct ManagerPairing {
    std::string code;
    std::string host;
    bool created = false;
};

std::optional<PairingCode> loadOrCreateWorkerPairingCode();
std::optional<std::string> loadWorkerPairingCode();
std::optional<std::string> regenerateWorkerPairingCode();
void showWorkerReadyDialog(const std::string& code);
std::optional<ManagerPairing> loadOrPromptManagerPairing();
std::optional<ManagerPairing> loadManagerPairing();
std::optional<ManagerPairing> repromptManagerPairing(const std::string& prefillHost,
                                                     const std::string& prefillCode);
void showPairingError(const std::string& message);

} // namespace openshare
