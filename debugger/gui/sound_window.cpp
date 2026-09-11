#include "sound_window.h"

// Dear ImGui
#include "imgui.h"
#include "imgui_internal.h"  // for ImDrawList access

#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Channel color table
// ---------------------------------------------------------------------------

static const ImVec4 kChannelColors[7] = {
    ImVec4(0.9f, 0.5f, 0.2f, 1.0f),  // Timer 0 — orange
    ImVec4(0.7f, 0.3f, 0.9f, 1.0f),  // Timer 1 — purple
    ImVec4(0.5f, 0.8f, 0.5f, 1.0f),  // Timer 2 — green
    ImVec4(1.0f, 1.0f, 0.3f, 1.0f),  // Noise   — yellow
    ImVec4(1.0f, 0.3f, 0.3f, 1.0f),  // AY A    — red
    ImVec4(0.3f, 1.0f, 0.3f, 1.0f),  // AY B    — green
    ImVec4(0.4f, 0.6f, 1.0f, 1.0f),  // AY C    — blue
};

static constexpr float kBarWidth     = 2.0f;
static constexpr float kChartHeight  = 36.0f;
static constexpr float kGridAlpha    = 0.15f;

// ---------------------------------------------------------------------------
// Draw a scrolling sound-log bar chart for one channel
// ---------------------------------------------------------------------------

static void drawSoundLog(const char *name, int channelIdx,
                         const float *ringBuffer, int writePos, int count,
                         bool active, float chartHeight)
{
    ImGui::PushID(channelIdx);

    // Label (dimmed when inactive)
    ImVec4 textColor = active ? ImVec4(1, 1, 1, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1);
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    ImGui::Text("%s", name);
    ImGui::PopStyleColor();

    // Allocate space for the bar chart
    ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, chartHeight);
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    // Background
    ImU32 bgColor = IM_COL32(20, 20, 20, 255);
    drawList->AddRectFilled(cursorPos,
                            ImVec2(cursorPos.x + canvasSize.x, cursorPos.y + canvasSize.y),
                            bgColor);

    // Horizontal grid lines at 0.25, 0.5, 0.75
    ImU32 gridColor = ImGui::GetColorU32(ImVec4(1, 1, 1, kGridAlpha));
    for (int g = 1; g <= 3; ++g) {
        float gy = cursorPos.y + canvasSize.y - (g * 0.25f) * canvasSize.y;
        drawList->AddLine(ImVec2(cursorPos.x, gy),
                          ImVec2(cursorPos.x + canvasSize.x, gy),
                          gridColor);
    }

    // Bars
    if (count > 0) {
        float availWidth = canvasSize.x;
        int maxBars = static_cast<int>(availWidth / kBarWidth);
        int numVisible = std::min(count, maxBars);
        ImVec4 col = kChannelColors[channelIdx];
        ImU32 barColor = ImGui::GetColorU32(col);

        for (int i = 0; i < numVisible; ++i) {
            // i=0 is the oldest visible sample, i=numVisible-1 is the newest
            int bufIdx = (writePos - numVisible + i + 1024) % 1024;
            float level = ringBuffer[bufIdx];

            if (level > 0.001f) {
                float barHeight = level * canvasSize.y;
                float x0 = cursorPos.x + (availWidth - numVisible * kBarWidth) + i * kBarWidth;
                float y0 = cursorPos.y + canvasSize.y - barHeight;
                float x1 = x0 + kBarWidth - 0.5f;  // small gap between bars
                float y1 = cursorPos.y + canvasSize.y;
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), barColor);
            }
        }
    }

    // Border
    ImU32 borderColor = ImGui::GetColorU32(ImVec4(0.4f, 0.4f, 0.4f, 0.6f));
    drawList->AddRect(cursorPos,
                      ImVec2(cursorPos.x + canvasSize.x, cursorPos.y + canvasSize.y),
                      borderColor);

    // Invisible dummy to reserve space in the ImGui layout
    ImGui::Dummy(canvasSize);

    ImGui::PopID();
}

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
    // Determine channel activity and compute levels
    // -----------------------------------------------------------------------

    bool chAActive = ampA > 0 && (snap.toneAEnabled || snap.noiseAEnabled);
    bool chBActive = ampB > 0 && (snap.toneBEnabled || snap.noiseBEnabled);
    bool chCActive = ampC > 0 && (snap.toneCEnabled || snap.noiseCEnabled);

    bool anyNoiseEnabled = (snap.noiseAEnabled && ampA > 0) ||
                           (snap.noiseBEnabled && ampB > 0) ||
                           (snap.noiseCEnabled && ampC > 0);

    // Timer channel state — activity based on dirty flag (ROM wrote since last snapshot)
    bool timerChActive[3] = {};
    for (int ch = 0; ch < 3; ++ch) {
        timerChActive[ch] = snap.timerChannels[ch].dirty;
    }

    // -----------------------------------------------------------------------
    // Compute per-channel levels and push into ring buffer
    // -----------------------------------------------------------------------

    bool paused = backend.isPaused();

    if (visualize_ && !paused) {
        float levels[NUM_CHANNELS];

        // Timer 0-2: bar when ROM wrote to counter (dirty), height = frequency
        for (int ch = 0; ch < 3; ++ch) {
            if (timerChActive[ch]) {
                uint16_t load = snap.timerChannels[ch].loadValue;
                float freq = load > 0 ? 1500000.0f / load : 0.0f;
                // Logarithmic mapping: 30Hz..15kHz → 0.3..1.0
                float norm = (freq > 0.0f) ?
                    (logf(freq / 30.0f) / logf(500.0f)) : 0.0f;
                levels[ch] = 0.3f + 0.7f * std::min(1.0f, std::max(0.0f, norm));
            } else {
                levels[ch] = 0.0f;
            }
        }

        // Noise: max amplitude among channels with noise enabled
        if (anyNoiseEnabled && noisePeriod > 0) {
            int maxNoiseAmp = 0;
            if (snap.noiseAEnabled) maxNoiseAmp = std::max(maxNoiseAmp, ampA);
            if (snap.noiseBEnabled) maxNoiseAmp = std::max(maxNoiseAmp, ampB);
            if (snap.noiseCEnabled) maxNoiseAmp = std::max(maxNoiseAmp, ampC);
            levels[3] = ampTable[maxNoiseAmp];
        } else {
            levels[3] = 0.0f;
        }

        // AY A/B/C: amplitude from table when active
        levels[4] = chAActive ? ampTable[ampA] : 0.0f;
        levels[5] = chBActive ? ampTable[ampB] : 0.0f;
        levels[6] = chCActive ? ampTable[ampC] : 0.0f;

        // Write into ring buffer
        for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
            soundLog_[ch][soundLogWrite_] = levels[ch];
        }
        soundLogWrite_ = (soundLogWrite_ + 1) % SOUND_LOG_CAPACITY;
        if (soundLogCount_ < SOUND_LOG_CAPACITY) ++soundLogCount_;
    }

    // -----------------------------------------------------------------------
    // Draw sound log bar charts
    // -----------------------------------------------------------------------

    // Timer channels
    ImGui::Text("i8253 Timer Channels:");
    ImGui::Spacing();
    {
        static const char *timerNames[] = { "Timer 0", "Timer 1", "Timer 2" };
        for (int ch = 0; ch < 3; ++ch) {
            drawSoundLog(timerNames[ch], ch,
                         soundLog_[ch], soundLogWrite_, soundLogCount_,
                         timerChActive[ch], kChartHeight);
        }
    }
    ImGui::Spacing();

    // Noise generator
    ImGui::Text("Noise Generator:");
    ImGui::Spacing();
    drawSoundLog("Noise", 3,
                 soundLog_[3], soundLogWrite_, soundLogCount_,
                 anyNoiseEnabled && noisePeriod > 0, kChartHeight);
    ImGui::Spacing();

    // AY-3-8912 tone channels
    ImGui::Text("AY-3-8912 Tone Channels:");
    ImGui::Spacing();
    {
        static const char *ayNames[] = { "Ch A", "Ch B", "Ch C" };
        bool ayActive[] = { chAActive, chBActive, chCActive };
        for (int ch = 0; ch < 3; ++ch) {
            drawSoundLog(ayNames[ch], 4 + ch,
                         soundLog_[4 + ch], soundLogWrite_, soundLogCount_,
                         ayActive[ch], kChartHeight + 10);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Timer register summary
    for (int ch = 0; ch < 3; ++ch) {
        int timerPeriod = snap.timerChannels[ch].loadValue;
        if (timerChActive[ch]) {
            float freq = 1500000.0f / timerPeriod;
            ImGui::Text("Timer %d: load=%d  mode=%d  freq=%.0f Hz",
                        ch, timerPeriod, snap.timerChannels[ch].mode, freq);
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
