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

    // AY channels are active when amplitude > 0 and tone/noise enabled
    bool chAActive = ampA > 0 && (snap.toneAEnabled || snap.noiseAEnabled);
    bool chBActive = ampB > 0 && (snap.toneBEnabled || snap.noiseBEnabled);
    bool chCActive = ampC > 0 && (snap.toneCEnabled || snap.noiseCEnabled);
    // Noise is audible if a channel has noise enabled AND amplitude > 0
    bool anyNoiseEnabled = (snap.noiseAEnabled && ampA > 0) ||
                           (snap.noiseBEnabled && ampB > 0) ||
                           (snap.noiseCEnabled && ampC > 0);
    bool anySoundActive = chAActive || chBActive || chCActive;

    // -----------------------------------------------------------------------
    // Timer channel state (i8253)
    // -----------------------------------------------------------------------

    bool timerChActive[3] = {};
    int  timerPeriod[3] = {};
    bool anyTimerActive = false;
    for (int ch = 0; ch < 3; ++ch) {
        timerPeriod[ch] = snap.timerChannels[ch].loadValue;
        // Channel active if period > 0 (ROM disables by writing 0)
        timerChActive[ch] = timerPeriod[ch] > 0;
        if (timerChActive[ch]) anyTimerActive = true;
    }

    // -----------------------------------------------------------------------
    // Generate waveforms (only when Visualize is enabled AND channel is active)
    // -----------------------------------------------------------------------

    float waveA[N], waveB[N], waveC[N], waveNoise[N];
    float waveTimer[3][N];
    memset(waveA, 0, sizeof(waveA));
    memset(waveB, 0, sizeof(waveB));
    memset(waveC, 0, sizeof(waveC));
    memset(waveNoise, 0, sizeof(waveNoise));
    memset(waveTimer, 0, sizeof(waveTimer));

    float levelA = 0, levelB = 0, levelC = 0;

    // Don't advance waveform generators while paused — CPU is stopped
    bool paused = backend.isPaused();

    if (visualize_ && !paused) {
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

            // --- Timer square wave generators (phase-continuous) ---
            for (int ch = 0; ch < 3; ++ch) {
                if (timerPeriod[ch] > 0) {
                    timerCount_[ch] += clocksPerSample;
                    if (timerCount_[ch] >= timerPeriod[ch]) {
                        timerCount_[ch] -= timerPeriod[ch];
                        timerOut_[ch] ^= 1;
                    }
                    waveTimer[ch][i] = timerOut_[ch] ? 0.5f : 0.0f;
                }
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

    // Helper: draw a channel waveform with status indicator
    auto drawChannel = [&](const char *name, const float *wave, float level,
                           bool active, int col, float height) {
        ImVec4 c(col == 0 ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) :
                 col == 1 ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) :
                 col == 2 ? ImVec4(0.4f, 0.6f, 1.0f, 1.0f) :
                 col == 3 ? ImVec4(1.0f, 1.0f, 0.3f, 1.0f) :
                 col == 4 ? ImVec4(0.9f, 0.5f, 0.2f, 1.0f) :
                 col == 5 ? ImVec4(0.7f, 0.3f, 0.9f, 1.0f) :
                            ImVec4(0.5f, 0.8f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, c);
        char label[64];
        if (active) {
            snprintf(label, sizeof(label), "%s", name);
        } else {
            snprintf(label, sizeof(label), "%s (idle)", name);
        }
        ImGui::PlotLines(label, wave, N, 0, nullptr,
                         0.0f, 1.0f, ImVec2(0, height));
        ImGui::PopStyleColor();
    };

    // Timer channels (standard Vector-06C hardware)
    ImGui::Text("i8253 Timer Channels:");
    ImGui::Spacing();
    {
        static const char *timerNames[] = { "Timer 0", "Timer 1", "Timer 2" };
        for (int ch = 0; ch < 3; ++ch) {
            drawChannel(timerNames[ch], waveTimer[ch], 0.0f,
                        timerChActive[ch], 4 + ch, 40);
        }
    }
    ImGui::Spacing();

    // Noise generator
    ImGui::Text("Noise Generator:");
    ImGui::Spacing();
    drawChannel("Noise", waveNoise, 0.0f, anyNoiseEnabled && noisePeriod > 0, 3, 40);
    ImGui::Spacing();

    // AY-3-8912 tone channels (optional expansion)
    ImGui::Text("AY-3-8912 Tone Channels:");
    ImGui::Spacing();

    drawChannel("Ch A", waveA, levelA, chAActive,       0, 50);
    drawChannel("Ch B", waveB, levelB, chBActive,       1, 50);
    drawChannel("Ch C", waveC, levelC, chCActive,       2, 50);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Timer register summary
    for (int ch = 0; ch < 3; ++ch) {
        if (timerChActive[ch]) {
            float freq = 1500000.0f / timerPeriod[ch];
            ImGui::Text("Timer %d: load=%d  mode=%d  freq=%.0f Hz",
                        ch, timerPeriod[ch], snap.timerChannels[ch].mode, freq);
        } else {
            ImGui::Text("Timer %d: idle", ch);
        }
    }
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
