#pragma once

#include <cstdint>
#include "idebug_backend.h"

// ---------------------------------------------------------------------------
// Sound Window
//
// Displays sound controls (Mute) and a scrolling "sound log" showing
// output level over time for all channels:
//   i8253 Timer 0-2, Noise, AY-3-8912 Ch A-C
//
// Bars scroll right-to-left while emulation runs; frozen when paused.
// ---------------------------------------------------------------------------

class SoundWindow
{
public:
    SoundWindow() {}

    // Render the window. Call every frame.
    void render(IDebugBackend &backend);

    // Check if window is visible
    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    bool &getVisibleRef() { return visible_; }

private:
    bool visible_ = true;
    bool muted_ = false;
    bool visualize_ = false;  // off by default: no level tracking

    // Sound log ring buffer
    // Channel order: Timer 0, Timer 1, Timer 2, Noise, AY A, AY B, AY C
    static constexpr int SOUND_LOG_CAPACITY = 1024;
    static constexpr int NUM_CHANNELS = 7;

    float soundLog_[NUM_CHANNELS][SOUND_LOG_CAPACITY] = {};
    int   soundLogWrite_ = 0;    // next write position
    int   soundLogCount_ = 0;    // number of valid samples (<= capacity)
};
