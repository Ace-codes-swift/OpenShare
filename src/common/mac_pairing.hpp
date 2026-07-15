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

// Worker: load the code from Keychain, or generate and store it.
std::optional<PairingCode> loadOrCreateWorkerPairingCode();

// Worker: read the stored code without creating one.
std::optional<std::string> loadWorkerPairingCode();

// Worker: generate a fresh code and overwrite the stored one
// (used on every manual launch so pairing always starts clean).
std::optional<std::string> regenerateWorkerPairingCode();

// Worker: show the pairing dialog with the manager code and this Mac's IP
// addresses. Blocks until the user dismisses it.
void showWorkerReadyDialog(const std::string& code);

// Manager: load the code + worker host, or ask for them once and store them.
std::optional<ManagerPairing> loadOrPromptManagerPairing();

// Manager: return whatever is already stored, without prompting.
std::optional<ManagerPairing> loadManagerPairing();

// Manager: always show the pairing dialog (prefilled with any known values)
// and persist the result. Used to fix a wrong address or re-pair.
std::optional<ManagerPairing> repromptManagerPairing(const std::string& prefillHost,
                                                     const std::string& prefillCode);

// Show a blocking error dialog to the user.
void showPairingError(const std::string& message);

// Worker: verify Screen Recording + Accessibility are granted. If anything is
// missing, trigger the system prompts, show an explanatory dialog with a
// button that opens the right System Settings pane, and return false (the
// app must be relaunched after granting Screen Recording anyway).
bool ensureCompanionPermissions();

} // namespace openshare
