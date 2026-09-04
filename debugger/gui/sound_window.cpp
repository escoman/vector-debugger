#include "sound_window.h"

// Dear ImGui
#include "imgui.h"

#include <cstdio>
#include <cmath>
#include <cstring>

static constexpr int WAVEFORM_SIZE = 256;

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void SoundWindow::render(IDebugBackend &backend)
{
    if (!visible_) return;

    ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Sound", &visible_)) {
        ImGui::End();
        return;
    }

    // Controls
    if (ImGui::Checkbox("Mute", &muted_)) {
        backend.setMuted(muted_);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Visualize", &visualize_);

    ImGui::Separator();
    ImGui::Spacing();

    // Get sound snapshot (register values)
    SoundSnapshot snap = backend.soundSnapshot();

    if (!snap.available) {
        ImGui::TextDisabled("(sound not available)");
        ImGui::End();
        return;
    }

    // -----------------------------------------------------------------------
    // Extract register values
    // -----------------------------------------------------------------------

    const int N = WAVEFORM_SIZE;

    int periodA = (snap.registers[0] & 0xFF) | ((snap.registers[1] & 0x0F) << 8);
    int periodB = (snap.registers[2] & 0xFF) | ((snap.registers[3] & 0x0F) << 8);
    int periodC = (snap.registers[4] & 0xFF) | ((snap.registers[5] & 0x0F) << 8);
    int ampA    = snap.registers[8]  & 0x0F;
    int ampB    = snap.registers[9]  & 0x0F;
    int ampC    = snap.registers[10] & 0x0F;
    int noisePeriod = snap.registers[6] & 0x1F;

    // AY amplitude lookup table (matches AY-3-8912 hardware)
    static const float ampTable[16] = {
        0.0f, 0.0137f, 0.0205f, 0.0291f,
        0.0423f, 0.0618f, 0.0847f, 0.1369f,
        0.1691f, 0.2647f, 0.3527f, 0.4499f,
        0.5704f, 0.6873f, 0.8482f, 1.0f
    };

    // -----------------------------------------------------------------------
    // Determine which channels are actually "active"
    // (i.e. the emulated program has configured them to produce sound)
    // -----------------------------------------------------------------------

    bool chAActive = ampA > 0 && (snap.toneAEnabled || snap.noiseAEnabled);
    bool chBActive = ampB > 0 && (snap.toneBEnabled || snap.noiseBEnabled);
    bool chCActive = ampC > 0 && (snap.toneCEnabled || snap.noiseCEnabled);
    bool anyNoiseEnabled = snap.noiseAEnabled || snap.noiseBEnabled || snap.noiseCEnabled;
    bool anySoundActive = chAActive || chBActive || chCActive;

    // -----------------------------------------------------------------------
    // Generate waveforms (only when Visualize is enabled AND channel is active)
    // -----------------------------------------------------------------------

    float waveA[N], waveB[N], waveC[N], waveNoise[N];
    memset(waveA, 0, sizeof(waveA));
    memset(waveB, 0, sizeof(waveB));
    memset(waveC, 0, sizeof(waveC));
    memset(waveNoise, 0, sizeof(waveNoise));

    float levelA = 0, levelB = 0, levelC = 0;

    if (visualize_) {
        const float clocksPerSample = 31.25f;  // ~1.5MHz / 48kHz

        for (int i = 0; i < N; ++i) {
            // --- Tone generators (phase-continuous) ---
            toneCountA_ += clocksPerSample;
            if (periodA > 0 && toneCountA_ >= periodA) {
                toneCountA_ -= periodA;
                toneOutA_ ^= 1;
            }

            toneCountB_ += clocksPerSample;
            if (periodB > 0 && toneCountB_ >= periodB) {
                toneCountB_ -= periodB;
                toneOutB_ ^= 1;
            }

            toneCountC_ += clocksPerSample;
            if (periodC > 0 && toneCountC_ >= periodC) {
                toneCountC_ -= periodC;
                toneOutC_ ^= 1;
            }

            // --- Noise generator (phase-continuous) ---
            noiseCount_ += clocksPerSample;
            int noiseThreshold = noisePeriod > 0 ? noisePeriod * 2 : 2;
            if (noiseCount_ >= noiseThreshold) {
                noiseCount_ -= noiseThreshold;
                noiseBit_ = noiseShift_ & 1;
                noiseShift_ = (noiseShift_ ^ ((noiseBit_) * 0x24000)) >> 1;
            }

            // --- AY mixer logic (only for active channels) ---
            auto chOutput = [&](bool toneEn, int toneOut,
                                bool noiseEn, int noiseOut, float amp) -> float {
                float mix = static_cast<float>((toneEn | toneOut) & (noiseEn | noiseOut));
                return mix * amp;
            };

            if (chAActive) {
                waveA[i] = chOutput(snap.toneAEnabled, toneOutA_,
                                    snap.noiseAEnabled, noiseBit_,
                                    ampTable[ampA]);
            }
            if (chBActive) {
                waveB[i] = chOutput(snap.toneBEnabled, toneOutB_,
                                    snap.noiseBEnabled, noiseBit_,
                                    ampTable[ampB]);
            }
            if (chCActive) {
                waveC[i] = chOutput(snap.toneCEnabled, toneOutC_,
                                    snap.noiseCEnabled, noiseBit_,
                                    ampTable[ampC]);
            }
            if (anyNoiseEnabled && noisePeriod > 0) {
                waveNoise[i] = noiseBit_ * 0.5f;
            }
        }

        // Compute RMS levels
        auto rms = [&](const float *data) -> float {
            float sum = 0;
            for (int i = 0; i < N; ++i) sum += data[i] * data[i];
            return sqrtf(sum / N);
        };
        levelA = chAActive ? rms(waveA) : 0.0f;
        levelB = chBActive ? rms(waveB) : 0.0f;
        levelC = chCActive ? rms(waveC) : 0.0f;
    }

    // -----------------------------------------------------------------------
    // Draw waveforms
    // -----------------------------------------------------------------------

    if (!visualize_) {
        ImGui::TextDisabled("(visualization off — enable \"Visualize\" to see waveforms)");
        ImGui::Spacing();
    } else if (!anySoundActive && !anyNoiseEnabled) {
        ImGui::TextDisabled("(no sound output — AY registers are idle)");
        ImGui::Spacing();
    }

    ImGui::Text("AY-3-8912 Channel Waveforms:");
    ImGui::Spacing();

    // Helper: draw a channel waveform with status indicator
    auto drawChannel = [&](const char *name, const float *wave, float level,
                           bool active, int col, float height) {
        ImVec4 c(col == 0 ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) :
                 col == 1 ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) :
                 col == 2 ? ImVec4(0.4f, 0.6f, 1.0f, 1.0f) :
                            ImVec4(1.0f, 1.0f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, c);
        char label[64];
        if (active) {
            snprintf(label, sizeof(label), "%s (level %.3f)", name, level);
        } else {
            snprintf(label, sizeof(label), "%s (idle)", name);
        }
        ImGui::PlotLines(label, wave, N, 0, nullptr,
                         0.0f, 1.0f, ImVec2(0, height));
        ImGui::PopStyleColor();
    };

    //                  name    wave   level  active        colorId  height
    drawChannel("Ch A", waveA, levelA, chAActive,       0, 50);
    drawChannel("Ch B", waveB, levelB, chBActive,       1, 50);
    drawChannel("Ch C", waveC, levelC, chCActive,       2, 50);
    drawChannel("Noise", waveNoise, 0.0f, anyNoiseEnabled && noisePeriod > 0, 3, 40);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // AY register summary
    ImGui::Text("Mixer (R7): %02X  Tone: %c%c%c  Noise: %c%c%c",
                snap.registers[7],
                snap.toneAEnabled ? 'A' : '.',
                snap.toneBEnabled ? 'B' : '.',
                snap.toneCEnabled ? 'C' : '.',
                snap.noiseAEnabled ? 'A' : '.',
                snap.noiseBEnabled ? 'B' : '.',
                snap.noiseCEnabled ? 'C' : '.');

    ImGui::Text("Amplitude: A=%X B=%X C=%X   Tone periods: A=%d B=%d C=%d",
                ampA, ampB, ampC, periodA, periodB, periodC);

    ImGui::End();
}
