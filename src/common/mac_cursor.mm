#include "mac_cursor.hpp"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openshare {

bool captureSystemCursor(double backingScale,
                         std::vector<uint8_t>& bgra,
                         int& width,
                         int& height,
                         int& hotX,
                         int& hotY) {
    if (backingScale < 0.5) {
        backingScale = 1.0;
    }

    @autoreleasepool {
        NSCursor* cur = [NSCursor currentSystemCursor];
        if (!cur) {
            cur = [NSCursor currentCursor];
        }
        if (!cur) {
            return false;
        }

        NSImage* image = cur.image;
        if (!image) {
            return false;
        }

        // NSImage.size / hotSpot are in points. Render into a pixel bitmap of
        // pointSize * backingScale so the cursor matches the capture buffer
        // (also in pixels). Using TIFF→CGImage alone often yields a Retina
        // representation while the hotspot stays in points, which made the
        // default arrow and I-beam draw 2× too large.
        const NSSize pointSize = image.size;
        if (pointSize.width < 1.0 || pointSize.height < 1.0) {
            return false;
        }

        const int pw = std::max(1, static_cast<int>(std::lround(pointSize.width * backingScale)));
        const int ph = std::max(1, static_cast<int>(std::lround(pointSize.height * backingScale)));
        if (pw > 512 || ph > 512) {
            return false;
        }

        bgra.assign(static_cast<size_t>(pw) * ph * 4, 0);

        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        if (!cs) {
            return false;
        }
        const uint32_t bitmapInfo = static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst) |
                                    static_cast<uint32_t>(kCGBitmapByteOrder32Little); // BGRA
        CGContextRef ctx = CGBitmapContextCreate(bgra.data(),
                                                 pw,
                                                 ph,
                                                 8,
                                                 static_cast<size_t>(pw) * 4,
                                                 cs,
                                                 bitmapInfo);
        CGColorSpaceRelease(cs);
        if (!ctx) {
            return false;
        }

        // CGBitmapContext is bottom-up. Draw with a normal (unflipped) AppKit
        // context, then flip rows so y=0 is the top — matching screen frames.
        NSGraphicsContext* previous = [NSGraphicsContext currentContext];
        NSGraphicsContext* nsCtx =
            [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO];
        [NSGraphicsContext setCurrentContext:nsCtx];
        [image drawInRect:NSMakeRect(0, 0, pw, ph)
                 fromRect:NSZeroRect
                operation:NSCompositingOperationCopy
                 fraction:1.0];
        [NSGraphicsContext setCurrentContext:previous];
        CGContextRelease(ctx);

        // Vertical flip: bottom-up bitmap → top-left BGRA for compositing.
        const size_t stride = static_cast<size_t>(pw) * 4;
        std::vector<uint8_t> flipped(bgra.size());
        for (int y = 0; y < ph; ++y) {
            std::memcpy(flipped.data() + static_cast<size_t>(y) * stride,
                        bgra.data() + static_cast<size_t>(ph - 1 - y) * stride,
                        stride);
        }
        bgra.swap(flipped);

        // Premultiplied → straight alpha for our blit helper.
        for (size_t i = 0; i < bgra.size(); i += 4) {
            const unsigned a = bgra[i + 3];
            if (a == 0 || a == 255) {
                continue;
            }
            bgra[i + 0] = static_cast<uint8_t>(std::min(255u, bgra[i + 0] * 255u / a));
            bgra[i + 1] = static_cast<uint8_t>(std::min(255u, bgra[i + 1] * 255u / a));
            bgra[i + 2] = static_cast<uint8_t>(std::min(255u, bgra[i + 2] * 255u / a));
        }

        const NSPoint hot = cur.hotSpot;
        width = pw;
        height = ph;
        hotX = std::clamp(static_cast<int>(std::lround(hot.x * backingScale)), 0, pw - 1);
        hotY = std::clamp(static_cast<int>(std::lround(hot.y * backingScale)), 0, ph - 1);
        return true;
    }
}

} // namespace openshare
