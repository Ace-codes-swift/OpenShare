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

#include "mac_pairing.hpp"

#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Security/Security.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace openshare {
namespace {

constexpr const char* kService = "com.atech.OpenShare.pairing";
// Separate entries: the companion's own code vs. the remote worker's code
// saved by the viewer. With one shared entry, running both apps on the same
// Mac would clobber each other's pairing.
constexpr const char* kWorkerAccount = "worker-code";
constexpr const char* kManagerAccount = "manager-code";

NSString* ns(const char* value) {
    return [NSString stringWithUTF8String:value];
}

std::string hostFilePath() {
    const char* home = std::getenv("HOME");
    if (!home) {
        return "/tmp/openshare_worker_host";
    }
    return std::string(home) + "/.openshare/worker_host";
}

std::string loadHostFile() {
    std::ifstream in(hostFilePath());
    if (!in) {
        return {};
    }
    std::string host;
    std::getline(in, host);
    return host;
}

void saveHostFile(const std::string& host) {
    const char* home = std::getenv("HOME");
    if (home) {
        std::string dir = std::string(home) + "/.openshare";
        mkdir(dir.c_str(), 0700);
    }
    std::ofstream out(hostFilePath(), std::ios::trunc);
    out << host << "\n";
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

// Distinguishes "no code stored yet" (generate one) from "stored but not
// readable" (access denied / keychain error) — regenerating in the latter
// case would silently invalidate an existing pairing.
std::optional<std::string> loadFromKeychain(const char* account, bool& accessError) {
    accessError = false;
    NSDictionary* query = @{
        (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService : ns(kService),
        (__bridge id)kSecAttrAccount : ns(account),
        (__bridge id)kSecReturnData : @YES,
        (__bridge id)kSecMatchLimit : (__bridge id)kSecMatchLimitOne,
    };

    CFTypeRef result = nullptr;
    const OSStatus status =
        SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
    if (status == errSecItemNotFound) {
        return std::nullopt;
    }
    if (status != errSecSuccess || !result) {
        accessError = true;
        return std::nullopt;
    }

    NSData* data = CFBridgingRelease(result);
    NSString* value = [[NSString alloc] initWithData:data
                                            encoding:NSUTF8StringEncoding];
    if (!value) {
        return std::nullopt;
    }
    return std::string(value.UTF8String);
}

bool storeInKeychain(const char* account, const std::string& value) {
    NSData* data = [NSData dataWithBytes:value.data() length:value.size()];
    NSDictionary* query = @{
        (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService : ns(kService),
        (__bridge id)kSecAttrAccount : ns(account),
    };
    NSDictionary* attributes = @{(__bridge id)kSecValueData : data};

    OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)query,
                                    (__bridge CFDictionaryRef)attributes);
    if (status == errSecItemNotFound) {
        NSMutableDictionary* add = [query mutableCopy];
        add[(__bridge id)kSecValueData] = data;
        status = SecItemAdd((__bridge CFDictionaryRef)add, nullptr);
    }
    return status == errSecSuccess;
}

void prepareDialogApp() {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp activateIgnoringOtherApps:YES];
}

std::string generateCode() {
    constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::array<uint8_t, 20> random{};
    if (SecRandomCopyBytes(kSecRandomDefault, random.size(), random.data()) !=
        errSecSuccess) {
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

void showWorkerCode(const std::string& code) {
    @autoreleasepool {
        prepareDialogApp();

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

        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"OpenShare worker is ready to pair";
        alert.informativeText = [NSString
            stringWithFormat:
                @"The worker is already listening. On your other Mac, open "
                 @"OpenShare and enter this Mac's address and the manager code "
                 @"below.\n\n%s\n\n"
                 @"Click Hide when you've copied the code — the worker stays "
                 @"running in the background (no Dock icon).",
            addrLine.c_str()];
        [alert addButtonWithTitle:@"Copy Code"];
        [alert addButtonWithTitle:@"Hide"];

        NSTextField* field =
            [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 360, 32)];
        field.stringValue = [NSString stringWithUTF8String:code.c_str()];
        field.editable = NO;
        field.selectable = YES;
        field.alignment = NSTextAlignmentCenter;
        field.font = [NSFont monospacedSystemFontOfSize:18
                                                 weight:NSFontWeightSemibold];
        alert.accessoryView = field;

        while ([alert runModal] == NSAlertFirstButtonReturn) {
            NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
            [pasteboard clearContents];
            [pasteboard setString:field.stringValue
                          forType:NSPasteboardTypeString];
        }
    }
}

// Returns {host, code} on success.
std::optional<std::pair<std::string, std::string>> promptManagerPairing(
    const std::string& prefillHost, const std::string& prefillCode) {
    prepareDialogApp();
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Pair with an OpenShare worker";
    alert.informativeText =
        @"Enter the worker Mac's address and the manager code shown by "
         "OpenShareCompanion.";
    [alert addButtonWithTitle:@"Pair"];
    [alert addButtonWithTitle:@"Cancel"];

    NSView* accessory =
        [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 360, 72)];

    NSTextField* hostField =
        [[NSTextField alloc] initWithFrame:NSMakeRect(0, 40, 360, 24)];
    hostField.placeholderString = @"Worker address (e.g. 192.168.1.20)";
    hostField.font = [NSFont systemFontOfSize:13];
    if (!prefillHost.empty() && prefillHost != "127.0.0.1") {
        hostField.stringValue = ns(prefillHost.c_str());
    }

    NSTextField* codeField =
        [[NSTextField alloc] initWithFrame:NSMakeRect(0, 4, 360, 28)];
    codeField.placeholderString = @"XXXX-XXXX-XXXX-XXXX-XXXX";
    codeField.font = [NSFont monospacedSystemFontOfSize:15
                                                 weight:NSFontWeightRegular];
    if (!prefillCode.empty()) {
        codeField.stringValue = ns(prefillCode.c_str());
    }

    [accessory addSubview:hostField];
    [accessory addSubview:codeField];
    alert.accessoryView = accessory;
    [alert.window setInitialFirstResponder:hostField];

    if ([alert runModal] != NSAlertFirstButtonReturn) {
        return std::nullopt;
    }

    NSCharacterSet* trim = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    NSString* host = [hostField.stringValue stringByTrimmingCharactersInSet:trim];
    NSString* code =
        [[codeField.stringValue uppercaseString] stringByTrimmingCharactersInSet:trim];

    if (host.length == 0 || code.length < 20) {
        NSAlert* error = [[NSAlert alloc] init];
        error.messageText = @"Incomplete pairing details";
        error.informativeText =
            @"Enter both the worker address and the complete manager code "
             "displayed on the worker Mac.";
        [error runModal];
        return std::nullopt;
    }
    return std::make_pair(std::string(host.UTF8String),
                          std::string(code.UTF8String));
}

} // namespace

std::optional<PairingCode> loadOrCreateWorkerPairingCode() {
    bool accessError = false;
    if (auto existing = loadFromKeychain(kWorkerAccount, accessError)) {
        return PairingCode{*existing, false};
    }
    if (accessError) {
        prepareDialogApp();
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"OpenShare cannot read its pairing code";
        alert.informativeText =
            @"A pairing code exists in the Keychain but access was denied.\n"
             "Relaunch and click \"Always Allow\" when macOS asks for Keychain "
             "access. Do NOT click Deny — that would break the existing pairing.";
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
        return std::nullopt;
    }

    std::string code = generateCode();
    if (code.empty() || !storeInKeychain(kWorkerAccount, code)) {
        return std::nullopt;
    }
    return PairingCode{std::move(code), true};
}

std::optional<std::string> loadWorkerPairingCode() {
    bool accessError = false;
    return loadFromKeychain(kWorkerAccount, accessError);
}

std::optional<std::string> regenerateWorkerPairingCode() {
    std::string code = generateCode();
    if (code.empty() || !storeInKeychain(kWorkerAccount, code)) {
        return std::nullopt;
    }
    return code;
}

void showWorkerReadyDialog(const std::string& code) {
    showWorkerCode(code);
}

std::optional<ManagerPairing> loadManagerPairing() {
    bool accessError = false;
    auto existingCode = loadFromKeychain(kManagerAccount, accessError);
    if (!existingCode) {
        return std::nullopt;
    }
    return ManagerPairing{*existingCode, loadHostFile(), false};
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
    if (!storeInKeychain(kManagerAccount, code)) {
        return std::nullopt;
    }
    saveHostFile(host);
    return ManagerPairing{code, host, true};
}

std::optional<ManagerPairing> repromptManagerPairing(const std::string& prefillHost,
                                                     const std::string& prefillCode) {
    auto entered = promptManagerPairing(prefillHost, prefillCode);
    if (!entered) {
        return std::nullopt;
    }
    const auto& [host, code] = *entered;
    if (!storeInKeychain(kManagerAccount, code)) {
        return std::nullopt;
    }
    saveHostFile(host);
    return ManagerPairing{code, host, true};
}

void showPairingError(const std::string& message) {
    prepareDialogApp();
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"OpenShare could not connect";
    alert.informativeText = ns(message.c_str());
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

bool ensureCompanionPermissions() {
    const bool screenOk = CGPreflightScreenCaptureAccess();
    const bool inputOk = AXIsProcessTrusted();
    if (screenOk && inputOk) {
        return true;
    }

    // Trigger the system prompts / add the app to the permission lists.
    if (!screenOk) {
        CGRequestScreenCaptureAccess();
    }
    if (!inputOk) {
        const void* keys[] = {kAXTrustedCheckOptionPrompt};
        const void* values[] = {kCFBooleanTrue};
        CFDictionaryRef opts =
            CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, nullptr, nullptr);
        AXIsProcessTrustedWithOptions(opts);
        CFRelease(opts);
    }

    prepareDialogApp();
    std::string missing;
    if (!screenOk) {
        missing += "  - Screen Recording (to stream the screen)\n";
    }
    if (!inputOk) {
        missing += "  - Accessibility (to control mouse and keyboard)\n";
    }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"OpenShareCompanion needs permissions";
    alert.informativeText = [NSString
        stringWithFormat:
            @"This Mac has not granted:\n%s\n"
            @"In System Settings > Privacy & Security, enable "
            @"OpenShareCompanion under each item above, then launch "
            @"OpenShareCompanion again.\n\n"
            @"If it is already listed but still not working, remove it "
            @"with the minus (-) button and add it back.",
        missing.c_str()];
    [alert addButtonWithTitle:@"Open System Settings"];
    [alert addButtonWithTitle:@"Quit"];

    if ([alert runModal] == NSAlertFirstButtonReturn) {
        NSString* pane = !screenOk
            ? @"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"
            : @"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility";
        [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:pane]];
    }
    return false;
}

} // namespace openshare
