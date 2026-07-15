#pragma once

#include <cstdint>

namespace openshare {

class MacInputInjector {
public:
    // Normalized [0,1] coords mapped to display pixel space using screenW/H.
    void mouseMove(float nx, float ny, int screenW, int screenH);
    void mouseButton(uint8_t button, bool down, float nx, float ny, int screenW, int screenH);
    // Deltas are in pixels (viewer pre-scales wheel ticks).
    void mouseWheel(int32_t dx, int32_t dy);
    void key(uint16_t keycode, uint32_t modifiers, bool down);

    static bool ensureAccessibilityPrompt();

private:
    // Which button is currently held, so moves become drag events.
    // -1 = none, else CGMouseButton value.
    int heldButton_ = -1;
};

} // namespace openshare
