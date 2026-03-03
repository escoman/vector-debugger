#include <sstream>
#include <string>
#include <cstdint>
#include <cassert>
#include <array>
#include <algorithm>
#include <functional>

#include "graphics32.h"
#include "libretro_scancodes.h"
#include "conv.h"

namespace colormap {
    inline constexpr uint32_t BACKGROUND = 0xb0b0a0;
    inline constexpr uint32_t KEY_BORDER = 0x404040;
    //inline constexpr uint32_t KEY_BORDER = 0x141414;
    inline constexpr uint32_t KEY_ALPHA = 0xbbb5a5;
    inline constexpr uint32_t KEY_BROWN = 0x402506;
    inline constexpr uint32_t KEY_GREEN = 0x7e7f65;
    inline constexpr uint32_t KEY_FN = 0x9e9776;
    inline constexpr uint32_t KEY_TEXT = 0x000000;
    inline constexpr uint32_t KEY_TEXT_BROWN = 0x808080;

    inline constexpr uint32_t KEY_BORDER_SELECT = 0xffffff;

    inline constexpr uint32_t LED_ON = 0xff4040;
    inline constexpr uint32_t LED_OFF = 0x401010;
};

template<std::size_t N>
bool contains(const int (&arr)[N], int value)
{
    return std::find(std::begin(arr), std::end(arr), value) != std::end(arr);
}

class VirtualKeyboard
{
    enum class unit_width_t { U0_5, U1, U1_5, U7 } ;

    struct key_info_t {
        int x, y;
        int width;
        uint32_t color;
        uint32_t text_color;
        int row, col; // normal row, col (col = 0..16)
        int coord; // row * 100 + col, cols >= 50 are numpad
        std::string legend_1;
        std::string legend_2;
        int scancode;
        bool pressed;
    };


    int debounce_count = 0;
    static constexpr int debounce_frames = 8;

    static constexpr int NUM_ROWS = 5;
    static constexpr int NUM_COLS = 17;

    static constexpr int TOP_BORDER = 1;
    static constexpr int BOTTOM_BORDER = 0;

    static constexpr int LED_RADIUS = 3;


public:
    int unit_w, unit_h;   // unit key width and height
    int xgap, ygap;       // key gap

    int select_row, select_col;
    int finger_row, finger_col;

    bool visible;

    std::array<int, 16> keys_down;
    std::array<int, 2> sticky_down;

    std::function<void(int)> on_keydown;
    std::function<void(int)> on_keyup;

    VirtualKeyboard(Graphics32& g, bool &ruslat_status) : gfx(g), ruslat_status(ruslat_status) {
        unit_w = 34;
        unit_h = 20;

        xgap = 1;
        ygap = 1;

        select_row = 2;
        select_col = 5;

        finger_row = 2;
        finger_col = 5;

        visible = false;
    }

    void set_joysticks(int joy0e, int joy0f)
    {
        if (joy0e == 0xff) {
            debounce_count = 0;
        }
        else {
            if (debounce_count && is_sticky(selected().scancode)) {
                return;
            }
        }

        if (debounce_count > 0) {
            --debounce_count;
            return;
        }

        if ((joy0e & 0x01) == 0) {
            // right
            key_up();
            move_finger(+1, 0);
            debounce_count = debounce_frames;
        }
        else if ((joy0e & 0x02) == 0) {
            // left
            key_up();
            move_finger(-1, 0);
            debounce_count = debounce_frames;
        }
        else if ((joy0e & 0x04) == 0) {
            // up
            key_up();
            move_finger(0, -1);
            debounce_count = debounce_frames;
        }
        else if ((joy0e & 0x08) == 0) {
            // down
            key_up();
            move_finger(0, +1);
            debounce_count = debounce_frames;
        }
        else if ((joy0e & 0x40) == 0) {
            // a
            key_down();
            debounce_count = debounce_frames;
        }
        else if ((joy0e & 0x80) == 0) {
            // b
            key_down();
            debounce_count = debounce_frames;
        }
        else {
            key_up();
        }
    }

    void prepare()
    {
        home();
        for (unsigned row = 0; row < std::size(top_text); ++row) 
        {
            if (row == 1) {
                space(unit_width_t::U0_5);
            }

            int col = 0;
            {
                std::string line = utf8_to_cp866(top_text[row]);
                std::stringstream ss(line);
                std::string word;

                std::string bline = utf8_to_cp866(bottom_text[row]);
                std::stringstream bs(bline);
                std::string bword;

                while (ss >> word && bs >> bword) {
                    key_info_t ki{};

                    make_key_info(ki, col, row, word, bword);
                    ki.row = row;
                    ki.col = col;
                    ki.scancode = scancodes[row][col];
                    key_map.at(row * NUM_COLS + col) = ki;
                    
                    col += 1;
                    cur_x += ki.width;
                }
            }

            if (row == 1) {
                space(unit_width_t::U0_5);
            }

            cur_x += pixel_width(unit_width_t::U1);
            col = 14;
            int fcol = 50; // only used for coord to reference colours

            {
                std::string line = utf8_to_cp866(num_text[row]);
                std::stringstream ss(line);
                std::string word;
                while (ss >> word) {
                    key_info_t ki{};
                    make_key_info(ki, fcol, row, word, "");
                    ki.row = row;
                    ki.col = col; // linear col, not fcol
                    ki.scancode = scancodes_num[row][fcol - 50];
                    key_map.at(row * NUM_COLS + col) = ki;

                    col += 1;
                    fcol += 1;
                    cur_x += ki.width;
                }
            }

            newline();
        }
    }

    void paint()
    {
        gfx.fillRect(0, 0, gfx.clip_rect.w, gfx.clip_rect.h, colormap::BACKGROUND);
        for (const key_info_t& ki : key_map) {
            draw_key(ki);
        }

        draw_ruslat();
    }

    int get_height() const
    {
        return unit_h * NUM_ROWS + TOP_BORDER + BOTTOM_BORDER;
    }

private:
    void draw_ruslat()
    {
        // in the gap
        //int x = unit_w * 13 + unit_w / 2;
        //int y = unit_h * 3 / 2;

        int x = unit_w / 3 + 1;
        int y = unit_h * 4 + unit_h - LED_RADIUS - 3 - ygap + TOP_BORDER;

        if (ruslat_status) {
            gfx.fillEllipse(x, y, LED_RADIUS * 3 / 2, LED_RADIUS, colormap::LED_ON);
        }
        else {
            gfx.fillEllipse(x, y, LED_RADIUS * 3 / 2, LED_RADIUS, colormap::LED_OFF);
        }
    }

    key_info_t& selected()
    {
        if ((unsigned)select_row >= NUM_ROWS || (unsigned)select_col >= NUM_COLS) {
            fprintf(stderr, "%s: invalid arg row=%d col=%d\n", __PRETTY_FUNCTION__, select_row, select_col);
        }
        key_info_t& ki = key_map.at(select_row * NUM_COLS + select_col);
        return ki;
    }

    void key_down()
    {
        key_info_t& ki = selected();
        int first_empty = keys_down.size();
        if (ki.scancode == 0) 
            return;

        for (unsigned i = 0; i < keys_down.size(); ++i) {
            if (keys_down[i] == ki.scancode) {
                return;
            }
            if (keys_down[i] == 0) {
                first_empty = i;
            }
        }

        if (is_sticky(ki.scancode)) {
            for (unsigned i = 0; i < sticky_down.size(); ++i) {
                if (sticky_down[i] == ki.scancode) {
                    sticky_down[i] = 0;
                    if (on_keyup) on_keyup(ki.scancode);
                    ki.pressed = false;
                    break;
                }
                else if (sticky_down[i] == 0) {
                    sticky_down[i] = ki.scancode;
                    if (on_keydown) on_keydown(ki.scancode);
                    ki.pressed = true;
                    break;
                }
            }
            return;
        }

        keys_down.at(first_empty) = ki.scancode;
        if (on_keydown) on_keydown(ki.scancode);
        ki.pressed = true;
    }

    void key_up()
    {
        key_info_t& ki = selected();
        if (ki.scancode == 0) 
            return;

        if (is_sticky(ki.scancode))
            return;

        for (unsigned i = 0; i < keys_down.size(); ++i) {
            if (keys_down[i] == ki.scancode) {
                keys_down[i] = 0;
                if (on_keyup) on_keyup(ki.scancode);
                ki.pressed = false;

                unstick_stickies();
                return;
            }
        }
    }

    void move_finger(int dx, int dy)
    {
        finger_row = std::clamp(finger_row + dy, 0, 4);
        finger_col = std::clamp(finger_col + dx, 0, NUM_COLS);

        select_row = finger_row;

        int prev_col = select_col;

        int again_finger_col = -1;
        while (again_finger_col != finger_col) {
            again_finger_col = finger_col;
            // search key under finger
            int finger_x = finger_col * unit_w - unit_w/2;
            for (int i = 0; i < NUM_COLS; ++i) {
                key_info_t& ki = key_map.at(select_row * NUM_COLS + i);
                if (ki.scancode == 0) continue;

                if (ki.x < finger_x && ki.x + ki.width >= finger_x) {
                    select_col = i;
                    if (select_col == prev_col) { // jump over wide key like space
                        finger_col = std::clamp(finger_col + dx, 0, NUM_COLS);
                    }
                    break;
                }
            }
        }
    }

    void make_key_info(key_info_t& ki, int col, int row, const std::string& L1, const std::string& L2)
    {
        ki.x = cur_x;
        ki.y = cur_y;
        ki.color = colormap::KEY_ALPHA;
        ki.text_color = colormap::KEY_TEXT;
        unit_width_t unit_width = VirtualKeyboard::unit_width_t::U1;
        ki.coord = row * 100 + col;
        if (contains(longKeys, ki.coord)) {
            unit_width = unit_width_t::U1_5;
            ki.color = colormap::KEY_BROWN;
            ki.text_color = colormap::KEY_TEXT_BROWN;
        }
        if (ki.coord == spaceKey) {
            unit_width = unit_width_t::U7;
            ki.color = colormap::KEY_GREEN;
        }
        if (contains(greenishKeys, ki.coord)) {
            ki.color = colormap::KEY_GREEN;
        }
        if (contains(mustardKeys, ki.coord)) {
            ki.color = colormap::KEY_FN;
        }
        ki.width = pixel_width(unit_width);

        ki.legend_1 = L1;
        ki.legend_2 = L2;
    }


    int pixel_width(unit_width_t uwidth)
    {
        switch (uwidth) {
            case unit_width_t::U0_5:  return unit_w / 2; 
            case unit_width_t::U1:    return unit_w; 
            case unit_width_t::U1_5:  return unit_w * 3 / 2;
            case unit_width_t::U7:    return unit_w * 7;
        }
        assert(0);
    }

    void key_rect(int x, int y, int w, int h, Graphics32::color_t color)
    {
        gfx.fillRect(x + 1, y, w - 2, 1, color);
        gfx.fillRect(x + 1, y + h - 1, w - 2, 1, color);
        gfx.fillRect(x, y + 1, 1, h - 2, color);
        gfx.fillRect(x + w - 1, y + 1, 1, h - 2, color);
    }

    void draw_key(const key_info_t& ki)
    {
        constexpr int border = 1;
        constexpr int xmargin = 2;
        constexpr int ymargin = 1;

        if (ki.scancode == 0) return;

        int w = ki.width - xgap;
        int h = unit_h - ygap;
        uint32_t bg_color = ki.color;
        if (ki.pressed) {
            // lighten up
            bg_color = gfx.mix(ki.color, gfx.RGB(255, 255, 255), 0.3f);
        }
        gfx.fillRect(ki.x + border, ki.y + border, w - 2 * border, h - 2 * border, bg_color);

        uint32_t border_color = colormap::KEY_BORDER;
        if (ki.row == select_row && ki.col == select_col) {
            border_color = colormap::KEY_BORDER_SELECT;
        }

        gfx.frontColor = ki.text_color;
        gfx.backColor = bg_color;

        int text1_x = ki.x + border + xmargin;
        int text1_y = ki.y + border + ymargin;

        int text2_x = ki.x + ki.width - xgap - xmargin - gfx.font->charWidth * ki.legend_2.size();
        int text2_y = ki.y + unit_h - ygap - border - ymargin - gfx.font->charHeight;

        if ((ki.coord % 100) >= 50 || ki.legend_2 == "___") {
            text1_x = ki.x + (ki.width - ki.legend_1.size() * gfx.font->charWidth) / 2;
            text1_y = ki.y + (unit_h - ygap - gfx.font->charHeight) / 2;
        }
        else if (ki.legend_2 == "_") {
            // for backspace/underscore key only center horizontally
            text1_x = ki.x + (ki.width - ki.legend_1.size() * gfx.font->charWidth) / 2;
            text2_x = ki.x + (ki.width - ki.legend_2.size() * gfx.font->charWidth) / 2;
        }

        gfx.setCursor(text1_x, text1_y);
        if (ki.legend_1 != "___") {
            for (const char c : ki.legend_1) {
                gfx.print(c);
            }
        }

        gfx.setCursor(text2_x, text2_y);
        if (ki.legend_2 != "___") {
            for (const char c : ki.legend_2) {
                gfx.print(c);
            }
        }

        key_rect(ki.x, ki.y, w, h, border_color);
    }

    void space(unit_width_t uwidth) 
    {
        cur_x += pixel_width(uwidth);
    }

    void newline()
    {
        cur_x = 0;
        cur_y += unit_h;
    }

    void home()
    {
        cur_x = 0;
        cur_y = TOP_BORDER;
    }

    bool is_sticky(int scancode) const
    {
        for (unsigned i = 0; i < std::size(scancodes_sticky); ++i) {
            if (scancodes_sticky[i] == scancode) return true;
        }
        return false;
    }

    void unstick_stickies()
    {
        bool nothing = true;
        for (unsigned i = 0; i < sticky_down.size(); ++i) {
            nothing &= sticky_down.at(i) == 0;
        }
        if (nothing) return;

        for (key_info_t& sticky : key_map) {
            for (unsigned i = 0; i < sticky_down.size(); ++i) {
                if (sticky_down.at(i) == sticky.scancode) {
                    if (on_keyup) on_keyup(sticky_down[i]);
                    sticky.pressed = false;
                    sticky_down[i] = 0;
                }
            }
        }
    }



private:
    Graphics32& gfx; 
    bool& ruslat_status;

    int cur_x, cur_y;

    std::array<key_info_t, NUM_COLS * NUM_ROWS> key_map;

    static constexpr const char * top_text[] = {
			"; 1 2 3 4 5 6 7 8 9 0 - /",
			"Й Ц У К Е Н Г Ш Щ З Х :",
			"УС Ф Ы В А П Р О Л Д Ж Э .",
			"СС Я Ч С М И Т Ь Б Ю , ВК",
			"РУС ТАБ ___ ПС ЗБ"};
    static constexpr const char * bottom_text[] = {
			"+ ! \" # ¤ % & ' ( ) ___ = ?",
			"J C U K E N G [ ] Z H *",
			"___ F Y W A P R O L D V \\ >",
			"___ Q ^ S M I T X B @ < ___",
			"LAT ___ ___ ___ _"};
    static constexpr const char * num_text[] = {
			"ВВОД БЛК СБР",
			"F1 F2 F3",
			"F4 F5 АР2",
			"↖ ↑ СТР",
			"← ↓ →"};
    static constexpr int longKeys[] = {300, 311, 400, 401, 403, 404}; // also brown
    static constexpr int greenishKeys[] = {402, 50,51,52, 252, 200};
    static constexpr int mustardKeys[] = {150,151,152, 250,251, 352};
    static constexpr int spaceKey = 402;

    static constexpr int scancodes[5][14] = {
        {SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_1, SDL_SCANCODE_2,
            SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
            SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0,
            SDL_SCANCODE_EQUALS, SDL_SCANCODE_SLASH},

        {SDL_SCANCODE_J, SDL_SCANCODE_C, SDL_SCANCODE_U, SDL_SCANCODE_K,
            SDL_SCANCODE_E, SDL_SCANCODE_N, SDL_SCANCODE_G, SDL_SCANCODE_LEFTBRACKET,
            SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_Z, SDL_SCANCODE_H,
            SDL_SCANCODE_APOSTROPHE},

        {SDL_SCANCODE_LCTRL, SDL_SCANCODE_F, SDL_SCANCODE_Y, SDL_SCANCODE_W,
            SDL_SCANCODE_A, SDL_SCANCODE_P, SDL_SCANCODE_R, SDL_SCANCODE_O,
            SDL_SCANCODE_L, SDL_SCANCODE_D, SDL_SCANCODE_V, SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_PERIOD},


        {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_Q, SDL_SCANCODE_GRAVE, SDL_SCANCODE_S,
            SDL_SCANCODE_M, SDL_SCANCODE_I, SDL_SCANCODE_T, SDL_SCANCODE_X,
            SDL_SCANCODE_B, SDL_SCANCODE_MINUS, SDL_SCANCODE_COMMA, SDL_SCANCODE_RETURN},

        {SDL_SCANCODE_F6, SDL_SCANCODE_TAB, 
            SDL_SCANCODE_SPACE,
            SDL_SCANCODE_LALT, SDL_SCANCODE_BACKSPACE}};

    static constexpr int scancodes_num[5][3] = {
        {SDL_SCANCODE_F11, -1, SDL_SCANCODE_F12},
        {SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3},
        {SDL_SCANCODE_F4, SDL_SCANCODE_F5, SDL_SCANCODE_ESCAPE},
        {SDL_SCANCODE_HOME, SDL_SCANCODE_UP, SDL_SCANCODE_END},
        {SDL_SCANCODE_LEFT, SDL_SCANCODE_DOWN, SDL_SCANCODE_RIGHT}};

    static constexpr int scancodes_blk[] = {SDL_SCANCODE_F11, -1, SDL_SCANCODE_F12};

    static constexpr int scancodes_sticky[] = {SDL_SCANCODE_LCTRL, SDL_SCANCODE_LSHIFT};

};

