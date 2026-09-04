#pragma once

#include <cstdint>
#include "idebug_backend.h"

// ---------------------------------------------------------------------------
// Sound Window
//
// Displays sound controls (Mute) and visual audio tracker showing
// waveform activity for AY-3-8912 channels (A, B, C) and noise.
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
    bool visualize_ = false;  // off by default: no waveform generation

    // Persistent tone/noise generator state for phase continuity across frames.
    // Without this, the waveform restarts from phase 0 every frame and looks frozen.
    float toneCountA_ = 0, toneCountB_ = 0, toneCountC_ = 0;
    int toneOutA_ = 1, toneOutB_ = 1, toneOutC_ = 1;
    int noiseShift_ = 1;
    int noiseBit_ = 1;
    float noiseCount_ = 0;
};
