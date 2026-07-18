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

#include "mac_inject.hpp"
#include "protocol.hpp"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>

namespace openshare {

// CGEvent mouse positions live in the display's global coordinate space,
// measured in points. The capture size passed in is in pixels, which differs
// from points by the backing scale factor on Retina displays, so we map onto
// the main display's point bounds instead of the pixel dimensions.
static CGPoint toPoint(float nx, float ny, int /*screenW*/, int /*screenH*/) {
    if (nx < 0.f) {
        nx = 0.f;
    }
    if (nx > 1.f) {
        nx = 1.f;
    }
    if (ny < 0.f) {
        ny = 0.f;
    }
    if (ny > 1.f) {
        ny = 1.f;
    }
    const CGRect bounds = CGDisplayBounds(CGMainDisplayID());
    const double x = bounds.origin.x + nx * (bounds.size.width - 1);
    const double y = bounds.origin.y + ny * (bounds.size.height - 1);
    return CGPointMake(x, y);
}

static CGEventFlags toFlags(uint32_t modifiers) {
    CGEventFlags flags = 0;
    if (modifiers & protocol::kModShift) {
        flags |= kCGEventFlagMaskShift;
    }
    if (modifiers & protocol::kModCtrl) {
        flags |= kCGEventFlagMaskControl;
    }
    if (modifiers & protocol::kModAlt) {
        flags |= kCGEventFlagMaskAlternate;
    }
    if (modifiers & protocol::kModCmd) {
        flags |= kCGEventFlagMaskCommand;
    }
    return flags;
}

bool MacInputInjector::ensureAccessibilityPrompt() {
    const void* keys[] = {kAXTrustedCheckOptionPrompt};
    const void* values[] = {kCFBooleanTrue};
    CFDictionaryRef opts = CFDictionaryCreate(
        kCFAllocatorDefault, keys, values, 1, nullptr, nullptr);
    const bool trusted = AXIsProcessTrustedWithOptions(opts);
    CFRelease(opts);
    return trusted;
}

void MacInputInjector::mouseMove(float nx, float ny, int screenW, int screenH) {
    const CGPoint pt = toPoint(nx, ny, screenW, screenH);
    // While a button is held, macOS only treats *Dragged events as a drag;
    // plain MouseMoved silently breaks click-and-drag.
    CGEventType type = kCGEventMouseMoved;
    CGMouseButton btn = kCGMouseButtonLeft;
    if (heldButton_ == kCGMouseButtonLeft) {
        type = kCGEventLeftMouseDragged;
    } else if (heldButton_ == kCGMouseButtonRight) {
        type = kCGEventRightMouseDragged;
        btn = kCGMouseButtonRight;
    } else if (heldButton_ == kCGMouseButtonCenter) {
        type = kCGEventOtherMouseDragged;
        btn = kCGMouseButtonCenter;
    }
    CGEventRef ev = CGEventCreateMouseEvent(nullptr, type, pt, btn);
    if (!ev) {
        return;
    }
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

void MacInputInjector::mouseButton(uint8_t button, bool down, float nx, float ny, int screenW, int screenH) {
    const CGPoint pt = toPoint(nx, ny, screenW, screenH);
    CGEventType type;
    CGMouseButton btn;
    switch (button) {
    case 1:
        btn = kCGMouseButtonRight;
        type = down ? kCGEventRightMouseDown : kCGEventRightMouseUp;
        break;
    case 2:
        btn = kCGMouseButtonCenter;
        type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
        break;
    default:
        btn = kCGMouseButtonLeft;
        type = down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
        break;
    }
    heldButton_ = down ? static_cast<int>(btn) : -1;
    CGEventRef ev = CGEventCreateMouseEvent(nullptr, type, pt, btn);
    if (!ev) {
        return;
    }
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

void MacInputInjector::mouseWheel(int32_t dx, int32_t dy) {
    // Pixel units: the viewer sends pre-scaled pixel deltas, which keeps
    // trackpad scrolling smooth instead of jumping whole lines.
    CGEventRef ev = CGEventCreateScrollWheelEvent(
        nullptr, kCGScrollEventUnitPixel, 2, dy, dx);
    if (!ev) {
        return;
    }
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

void MacInputInjector::key(uint16_t keycode, uint32_t modifiers, bool down) {
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(keycode), down);
    if (!ev) {
        return;
    }
    CGEventSetFlags(ev, toFlags(modifiers));
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

} // namespace openshare
