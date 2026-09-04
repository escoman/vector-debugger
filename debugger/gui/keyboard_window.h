#pragma once

#include <cstdint>
#include <vector>
#include <set>
#include "idebug_backend.h"

// ---------------------------------------------------------------------------
// KeyboardWindow — Virtual Vector-06C keyboard (ImGui/ImDrawList).
//
// Renders the same 5-row layout as the PSP port's vkbd.
// Mouse click sends key press/release through IDebugBackend.
// Modifier keys (SS/US) toggle sticky; other keys are momentary.
// ---------------------------------------------------------------------------

class KeyboardWindow
{
public:
    KeyboardWindow();

    void render(IDebugBackend &backend);

    bool &getVisibleRef() { return visible_; }
    void requestRefresh() {}

private:
    enum KeyColor {
        KC_ALPHA,    // beige — letter/number keys
        KC_BROWN,    // dark brown — УС, Ж, Э, ВК, СС, АР2
        KC_GREEN,    // olive — УС, ЗБ, ПС, space, tab
        KC_FN,       // golden — F1-F5, РУС, ТАБ
    };

    struct KeyDef {
        float col, row;      // grid position (col may be fractional for offsets)
        float w, h;          // grid units
        int scancode;        // SDL_Scancode value (0 = spacer)
        const char *label;   // Latin legend
        KeyColor color;
    };

    bool visible_ = true;

    // Key geometry
    static constexpr float kKeyW = 32.0f;
    static constexpr float kKeyH = 28.0f;
    static constexpr float kGap  = 2.0f;

    // Mouse/press state
    int pressedScancode_ = 0;          // momentary key held by mouse
    std::set<int> stickyKeys_;         // toggled modifiers (SS/US)
    int selectedScancode_ = 0;         // last clicked (for visual feedback)
    float selectionTimer_ = 0.0f;

    // Layout
    static const std::vector<KeyDef> &getKeyLayout();
};
