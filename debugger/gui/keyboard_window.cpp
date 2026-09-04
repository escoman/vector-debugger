#include "keyboard_window.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "SDL.h"

#include <cstdio>
#include <cstring>
#include <cmath>

// SDL scancode constants (from SDL2/SDL_scancode.h)
enum {
    SC_A = 4,  SC_B = 5,  SC_C = 6,  SC_D = 7,
    SC_E = 8,  SC_F = 9,  SC_G = 10, SC_H = 11,
    SC_I = 12, SC_J = 13, SC_K = 14, SC_L = 15,
    SC_M = 16, SC_N = 17, SC_O = 18, SC_P = 19,
    SC_Q = 20, SC_R = 21, SC_S = 22, SC_T = 23,
    SC_U = 24, SC_V = 25, SC_W = 26, SC_X = 27,
    SC_Y = 28, SC_Z = 29,
    SC_1 = 30, SC_2 = 31, SC_3 = 32, SC_4 = 33,
    SC_5 = 34, SC_6 = 35, SC_7 = 36, SC_8 = 37,
    SC_9 = 38, SC_0 = 39,
    SC_RETURN    = 40,
    SC_ESCAPE    = 41,
    SC_BACKSPACE = 42,
    SC_TAB       = 43,
    SC_SPACE     = 44,
    SC_MINUS     = 45,
    SC_EQUALS    = 46,
    SC_LBRACKET  = 47,
    SC_RBRACKET  = 48,
    SC_BACKSLASH = 49,
    SC_SEMICOLON = 51,
    SC_APOSTROPHE= 52,
    SC_GRAVE     = 53,
    SC_COMMA     = 54,
    SC_PERIOD    = 55,
    SC_SLASH     = 56,
    SC_F1 = 58, SC_F2 = 59, SC_F3 = 60, SC_F4 = 61, SC_F5 = 62,
    SC_F6 = 63, SC_F7 = 64,
    SC_HOME  = 106,
    SC_UP    = 82,
    SC_END   = 107,
    SC_LEFT  = 80,
    SC_RIGHT = 79,
    SC_DOWN  = 81,
    SC_LSHIFT = 225,
    SC_LCTRL  = 224,
    SC_LALT   = 226,
};

// ---------------------------------------------------------------------------
// Key layout — matches the PSP port's vkbd layout
// ---------------------------------------------------------------------------

const std::vector<KeyboardWindow::KeyDef> &KeyboardWindow::getKeyLayout()
{
    static const std::vector<KeyDef> layout = {
        // Row 0: ; 1 2 3 4 5 6 7 8 9 0 - /
        {0.0f, 0, 1, 1, SC_SEMICOLON, ";",  KC_ALPHA},
        {1.0f, 0, 1, 1, SC_1,           "1",  KC_ALPHA},
        {2.0f, 0, 1, 1, SC_2,           "2",  KC_ALPHA},
        {3.0f, 0, 1, 1, SC_3,           "3",  KC_ALPHA},
        {4.0f, 0, 1, 1, SC_4,           "4",  KC_ALPHA},
        {5.0f, 0, 1, 1, SC_5,           "5",  KC_ALPHA},
        {6.0f, 0, 1, 1, SC_6,           "6",  KC_ALPHA},
        {7.0f, 0, 1, 1, SC_7,           "7",  KC_ALPHA},
        {8.0f, 0, 1, 1, SC_8,           "8",  KC_ALPHA},
        {9.0f, 0, 1, 1, SC_9,           "9",  KC_ALPHA},
        {10.0f,0, 1, 1, SC_0,           "0",  KC_ALPHA},
        {11.0f,0, 1, 1, SC_EQUALS,      "=",  KC_ALPHA},
        {12.0f,0, 1, 1, SC_SLASH,       "/",  KC_ALPHA},

        // Row 1 (indented 0.5u): J C U K E N G [ ] Z H '
        {0.5f, 1, 1, 1, SC_J,           "J",  KC_ALPHA},
        {1.5f, 1, 1, 1, SC_C,           "C",  KC_ALPHA},
        {2.5f, 1, 1, 1, SC_U,           "U",  KC_ALPHA},
        {3.5f, 1, 1, 1, SC_K,           "K",  KC_ALPHA},
        {4.5f, 1, 1, 1, SC_E,           "E",  KC_ALPHA},
        {5.5f, 1, 1, 1, SC_N,           "N",  KC_ALPHA},
        {6.5f, 1, 1, 1, SC_G,           "G",  KC_ALPHA},
        {7.5f, 1, 1, 1, SC_LBRACKET,    "[",  KC_ALPHA},
        {8.5f, 1, 1, 1, SC_RBRACKET,    "]",  KC_ALPHA},
        {9.5f, 1, 1, 1, SC_Z,           "Z",  KC_ALPHA},
        {10.5f,1, 1, 1, SC_H,           "H",  KC_ALPHA},
        {11.5f,1, 1, 1, SC_APOSTROPHE,  "'",  KC_ALPHA},

        // Row 2: US F Y W A P R O L D V \ .
        {0.0f, 2, 1, 1, SC_LCTRL,       "US", KC_GREEN},
        {1.0f, 2, 1, 1, SC_F,           "F",  KC_ALPHA},
        {2.0f, 2, 1, 1, SC_Y,           "Y",  KC_ALPHA},
        {3.0f, 2, 1, 1, SC_W,           "W",  KC_ALPHA},
        {4.0f, 2, 1, 1, SC_A,           "A",  KC_ALPHA},
        {5.0f, 2, 1, 1, SC_P,           "P",  KC_ALPHA},
        {6.0f, 2, 1, 1, SC_R,           "R",  KC_ALPHA},
        {7.0f, 2, 1, 1, SC_O,           "O",  KC_ALPHA},
        {8.0f, 2, 1, 1, SC_L,           "L",  KC_ALPHA},
        {9.0f, 2, 1, 1, SC_D,           "D",  KC_ALPHA},
        {10.0f,2, 1, 1, SC_V,           "V",  KC_ALPHA},
        {11.0f,2, 1, 1, SC_BACKSLASH,   "\\", KC_BROWN},
        {12.0f,2, 1, 1, SC_PERIOD,      ".",  KC_ALPHA},

        // Row 3: SS Q ^ S M I T X B - , VK
        {0.0f, 3, 1, 1, SC_LSHIFT,      "SS", KC_BROWN},
        {1.0f, 3, 1, 1, SC_Q,           "Q",  KC_ALPHA},
        {2.0f, 3, 1, 1, SC_GRAVE,       "^",  KC_ALPHA},
        {3.0f, 3, 1, 1, SC_S,           "S",  KC_ALPHA},
        {4.0f, 3, 1, 1, SC_M,           "M",  KC_ALPHA},
        {5.0f, 3, 1, 1, SC_I,           "I",  KC_ALPHA},
        {6.0f, 3, 1, 1, SC_T,           "T",  KC_ALPHA},
        {7.0f, 3, 1, 1, SC_X,           "X",  KC_ALPHA},
        {8.0f, 3, 1, 1, SC_B,           "B",  KC_ALPHA},
        {9.0f, 3, 1, 1, SC_MINUS,       "-",  KC_ALPHA},
        {10.0f,3, 1, 1, SC_COMMA,       ",",  KC_ALPHA},
        {11.0f,3, 1, 1, SC_RETURN,      "VK", KC_GREEN},

        // Row 4: RUS TAB PS ZB
        {0.0f, 4, 1.5f, 1, SC_F6,       "RUS",KC_FN},
        {1.5f, 4, 1.5f, 1, SC_TAB,      "TAB",KC_FN},
        {3.0f, 4, 5.0f, 1, SC_SPACE,    "",   KC_GREEN},
        {8.0f, 4, 1.5f, 1, SC_LALT,     "PS", KC_GREEN},
        {9.5f, 4, 1.5f, 1, SC_BACKSPACE,"BS", KC_ALPHA},

        // --- Numpad (3 columns, offset 14.5u) ---
        // Matches PSP vkbd: VVOD=END(CTP), BLK=F7, SBR=HOME(^\)

        // Row 0: VVOD BLK SBR
        {14.5f, 0, 1, 1, SC_END,        "VVOD",KC_BROWN},
        {15.5f, 0, 1, 1, SC_F7,         "BLK", KC_BROWN},
        {16.5f, 0, 1, 1, SC_HOME,       "SBR", KC_BROWN},

        // Row 1: F1 F2 F3
        {14.5f, 1, 1, 1, SC_F1,        "F1",  KC_FN},
        {15.5f, 1, 1, 1, SC_F2,        "F2",  KC_FN},
        {16.5f, 1, 1, 1, SC_F3,        "F3",  KC_FN},

        // Row 2: F4 F5 AR2
        {14.5f, 2, 1, 1, SC_F4,        "F4",  KC_FN},
        {15.5f, 2, 1, 1, SC_F5,        "F5",  KC_FN},
        {16.5f, 2, 1, 1, SC_ESCAPE,    "AR2", KC_BROWN},

        // Row 3: Home Up End
        {14.5f, 3, 1, 1, SC_HOME,      "HM",  KC_ALPHA},
        {15.5f, 3, 1, 1, SC_UP,        "UP",  KC_ALPHA},
        {16.5f, 3, 1, 1, SC_END,       "END", KC_ALPHA},

        // Row 4: Left Down Right
        {14.5f, 4, 1, 1, SC_LEFT,      "LT",  KC_ALPHA},
        {15.5f, 4, 1, 1, SC_DOWN,      "DN",  KC_ALPHA},
        {16.5f, 4, 1, 1, SC_RIGHT,     "RT",  KC_ALPHA},
    };
    return layout;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

KeyboardWindow::KeyboardWindow()
{
}

// ---------------------------------------------------------------------------
// SDL event forwarding (physical keyboard → emulator)
// ---------------------------------------------------------------------------

bool KeyboardWindow::handleSdlEvent(const SDL_Event &event, IDebugBackend &backend)
{
    if (!keyboardLock_) return false;

    if (event.type == SDL_KEYDOWN && !event.key.repeat) {
        int sc = event.key.keysym.scancode;
        if (activeKeys_.insert(sc).second) {
            backend.pressKey(sc);
        }
        return true;
    }
    if (event.type == SDL_KEYUP) {
        int sc = event.key.keysym.scancode;
        auto it = activeKeys_.find(sc);
        if (it != activeKeys_.end()) {
            activeKeys_.erase(it);
            backend.releaseKey(sc);
        }
        return true;
    }
    return false;
}

void KeyboardWindow::releaseAllKeys(IDebugBackend &backend)
{
    for (int sc : activeKeys_) {
        backend.releaseKey(sc);
    }
    activeKeys_.clear();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void KeyboardWindow::render(IDebugBackend &backend)
{
    // Release physical keys when window becomes hidden
    static bool wasVisible = visible_;
    if (!visible_ && wasVisible) {
        releaseAllKeys(backend);
    }
    wasVisible = visible_;

    if (!visible_) return;

    const auto &keys = getKeyLayout();
    const float pad = 6.0f;
    const float totalW = 17.5f * kKeyW + 2.0f * pad;
    const float totalH = 5.0f * kKeyH + 2.0f * pad;

    ImGui::SetNextWindowSize(ImVec2(totalW + 16, totalH + 40), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Keyboard", &visible_)) {
        ImGui::End();
        return;
    }

    // Lock checkbox — forward physical keyboard to emulator
    ImGui::Checkbox("Lock", &keyboardLock_);
    if (!keyboardLock_ && !activeKeys_.empty()) {
        releaseAllKeys(backend);
    }

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize(totalW, totalH);

    // Background
    drawList->AddRectFilled(cursorScreenPos,
        ImVec2(cursorScreenPos.x + canvasSize.x, cursorScreenPos.y + canvasSize.y),
        IM_COL32(40, 40, 48, 255));

    // Invisible interactive area
    ImGui::InvisibleButton("##kbd_canvas", canvasSize);
    bool isItemActive = ImGui::IsItemActive();

    // Get mouse position and determine which key is hovered
    ImVec2 mousePos = ImGui::GetMousePos();
    float localMx = mousePos.x - cursorScreenPos.x - pad;
    float localMy = mousePos.y - cursorScreenPos.y - pad;

    int hoveredScancode = 0;
    for (const auto &k : keys) {
        if (k.scancode == 0) continue;
        float kx = k.col * kKeyW + pad;
        float ky = k.row * kKeyH + pad;
        float kw = k.w * kKeyW - kGap;
        float kh = k.h * kKeyH - kGap;
        if (localMx >= kx && localMx < kx + kw &&
            localMy >= ky && localMy < ky + kh) {
            hoveredScancode = k.scancode;
            break;
        }
    }

    // --- Mouse interaction ---
    bool mouseClicked = ImGui::IsMouseClicked(0) && isItemActive;
    bool mouseReleased = ImGui::IsMouseReleased(0);

    // Release momentary key
    if (mouseReleased && pressedScancode_ != 0) {
        backend.releaseKey(pressedScancode_);
        pressedScancode_ = 0;
    }

    // Press new key
    if (mouseClicked && hoveredScancode != 0) {
        bool isSticky = (hoveredScancode == SC_LSHIFT ||
                         hoveredScancode == SC_LCTRL);
        if (isSticky) {
            auto it = stickyKeys_.find(hoveredScancode);
            if (it != stickyKeys_.end()) {
                stickyKeys_.erase(it);
                backend.releaseKey(hoveredScancode);
            } else {
                stickyKeys_.insert(hoveredScancode);
                backend.pressKey(hoveredScancode);
            }
        } else {
            // Release previous momentary key
            if (pressedScancode_ != 0) {
                backend.releaseKey(pressedScancode_);
            }
            pressedScancode_ = hoveredScancode;
            backend.pressKey(hoveredScancode);
        }
        selectedScancode_ = hoveredScancode;
        selectionTimer_ = 0.15f;
    }

    // Fade selection timer
    if (selectionTimer_ > 0.0f) {
        selectionTimer_ -= ImGui::GetIO().DeltaTime;
        if (selectionTimer_ < 0.0f) selectionTimer_ = 0.0f;
    }

    // --- Draw keys ---
    for (const auto &k : keys) {
        if (k.scancode == 0) continue;

        float kx = k.col * kKeyW + pad;
        float ky = k.row * kKeyH + pad;
        float kw = k.w * kKeyW - kGap;
        float kh = k.h * kKeyH - kGap;

        ImVec2 pMin(cursorScreenPos.x + kx, cursorScreenPos.y + ky);
        ImVec2 pMax(cursorScreenPos.x + kx + kw, cursorScreenPos.y + ky + kh);

        bool isPressed = (k.scancode == pressedScancode_) ||
                         (stickyKeys_.count(k.scancode) > 0) ||
                         (activeKeys_.count(k.scancode) > 0);
        bool isSelected = (k.scancode == selectedScancode_) &&
                          (selectionTimer_ > 0.0f);

        // Key background color
        ImU32 bgColor;
        switch (k.color) {
            case KC_ALPHA: bgColor = IM_COL32(0xBB, 0xB5, 0xA5, 255); break;
            case KC_BROWN: bgColor = IM_COL32(0x60, 0x45, 0x26, 255); break;
            case KC_GREEN: bgColor = IM_COL32(0x7E, 0x7F, 0x65, 255); break;
            case KC_FN:    bgColor = IM_COL32(0x9E, 0x97, 0x76, 255); break;
        }

        if (isPressed) {
            // Lighten the key color (mix with white ~30%)
            switch (k.color) {
                case KC_ALPHA: bgColor = IM_COL32(0xD0, 0xCD, 0xC1, 255); break;
                case KC_BROWN: bgColor = IM_COL32(0x8A, 0x6F, 0x52, 255); break;
                case KC_GREEN: bgColor = IM_COL32(0xA0, 0xA1, 0x8E, 255); break;
                case KC_FN:    bgColor = IM_COL32(0xB8, 0xB2, 0x9C, 255); break;
            }
        }

        drawList->AddRectFilled(pMin, pMax, bgColor, 2.0f);

        // Border
        ImU32 borderColor = isSelected ? IM_COL32(0xFF, 0xFF, 0xFF, 255)
                                       : IM_COL32(0x40, 0x40, 0x40, 255);
        drawList->AddRect(pMin, pMax, borderColor, 2.0f);

        // Label
        if (k.label[0] != '\0') {
            ImVec2 textSize = ImGui::CalcTextSize(k.label);
            ImVec2 textPos(pMin.x + (kw - textSize.x) * 0.5f,
                           pMin.y + (kh - textSize.y) * 0.5f);
            ImU32 textColor = (k.color == KC_BROWN)
                ? IM_COL32(0x80, 0x80, 0x80, 255)
                : IM_COL32(0x00, 0x00, 0x00, 255);
            drawList->AddText(textPos, textColor, k.label);
        }
    }

    // РУС/LAT LED indicator
    {
        float ledX = cursorScreenPos.x + pad + 0.5f * kKeyW;
        float ledY = cursorScreenPos.y + pad + 4.0f * kKeyH + kKeyH * 0.7f;
        float ledR = 4.0f;
        // The LED is off by default (no way to query ruslat from backend yet)
        drawList->AddCircleFilled(ImVec2(ledX, ledY), ledR,
            IM_COL32(0x40, 0x10, 0x10, 255));
    }

    ImGui::End();
}
