/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "button_mapping.hpp"

#include <Limelight.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace ButtonMapping {

// ---- names ------------------------------------------------------------------

struct TargetInfo {
    const char *token;
    const char *label;
    const char *short_name;
};

// Indexed by PAD_TARGET
static const TargetInfo TARGETS[PAD_TARGET_COUNT] = {
    {"none", "Nothing", ""},
    {"a", "A", "A"},
    {"b", "B", "B"},
    {"x", "X", "X"},
    {"y", "Y", "Y"},
    {"lb", "LB (left bumper)", "LB"},
    {"rb", "RB (right bumper)", "RB"},
    {"lt", "LT (left trigger)", "LT"},
    {"rt", "RT (right trigger)", "RT"},
    {"view", "View / Back", "VIEW"},
    {"menu", "Menu / Start", "MENU"},
    {"guide", "Guide (Xbox button)", "GUIDE"},
    {"l3", "L3 (left stick click)", "L3"},
    {"r3", "R3 (right stick click)", "R3"},
    {"l3r3", "L3 + R3 together", "L3+R3"},
    {"up", "D-pad up", "UP"},
    {"down", "D-pad down", "DOWN"},
    {"left", "D-pad left", "LEFT"},
    {"right", "D-pad right", "RIGHT"},
};

static const char *SOURCE_TOKENS[PAD_SOURCE_COUNT] = {
    "a", "b", "x", "y", "l", "r", "zl", "zr", "start", "select"};
static const char *SOURCE_LABELS[PAD_SOURCE_COUNT] = {
    "A button", "B button", "X button", "Y button",     "L button",
    "R button", "ZL button", "ZR button", "START button", "SELECT button"};

static const char *ZONE_TOKENS[TOUCH_ZONE_COUNT] = {"topleft", "topmiddle",
                                                    "topright", "bottom"};
static const char *ZONE_LABELS[TOUCH_ZONE_COUNT] = {
    "Gamepad view: top left", "Gamepad view: top middle",
    "Gamepad view: top right", "Gamepad view: bottom"};

static int clamp_target(int t) {
    return (t >= 0 && t < PAD_TARGET_COUNT) ? t : PAD_NONE;
}

const char *target_token(int t) { return TARGETS[clamp_target(t)].token; }
const char *target_label(int t) { return TARGETS[clamp_target(t)].label; }
const char *target_short(int t) { return TARGETS[clamp_target(t)].short_name; }
const char *source_label(int s) {
    return (s >= 0 && s < PAD_SOURCE_COUNT) ? SOURCE_LABELS[s] : "";
}
const char *zone_label(int z) {
    return (z >= 0 && z < TOUCH_ZONE_COUNT) ? ZONE_LABELS[z] : "";
}

// ---- defaults and presets ---------------------------------------------------

static const unsigned char DEFAULT_BUTTONS[PAD_SOURCE_COUNT] = {
    PAD_A,  PAD_B,  PAD_X,  PAD_Y,    PAD_LB,
    PAD_RB, PAD_LT, PAD_RT, PAD_MENU, PAD_VIEW};
static const unsigned char DEFAULT_TOUCH[TOUCH_ZONE_COUNT] = {
    PAD_L3, PAD_L3R3, PAD_R3, PAD_GUIDE};

static void preset_buttons(int preset, unsigned char out[PAD_SOURCE_COUNT]) {
    memcpy(out, DEFAULT_BUTTONS, PAD_SOURCE_COUNT);
    if (preset == 1 || preset == 3) {
        // The 3DS has A on the right like a Nintendo pad; send the button in
        // the same position on an Xbox pad instead
        out[SRC_A] = PAD_B;
        out[SRC_B] = PAD_A;
        out[SRC_X] = PAD_Y;
        out[SRC_Y] = PAD_X;
    }
    if (preset == 2 || preset == 3) {
        out[SRC_L] = PAD_LT;
        out[SRC_R] = PAD_RT;
        out[SRC_ZL] = PAD_LB;
        out[SRC_ZR] = PAD_RB;
    }
}

void set_defaults(PCONFIGURATION cfg) {
    memcpy(cfg->button_map, DEFAULT_BUTTONS, PAD_SOURCE_COUNT);
    memcpy(cfg->touch_map, DEFAULT_TOUCH, TOUCH_ZONE_COUNT);
}

void apply_legacy_swaps(PCONFIGURATION cfg, bool swap_face,
                        bool swap_shoulders) {
    apply_preset(cfg, (swap_face ? 1 : 0) + (swap_shoulders ? 2 : 0));
}

static const char *PRESET_LABELS[PRESET_COUNT] = {
    "Standard", "Xbox positions", "Shoulders as triggers",
    "Xbox positions + shoulders"};
static const char *PRESET_DESCS[PRESET_COUNT] = {
    "Each 3DS button sends the controller button with the same letter: A "
    "sends A, L sends LB, ZL sends LT. Matches on-screen prompts by name.",
    "Face buttons send the button in the same position on an Xbox pad: 3DS "
    "A (right) sends B, B (bottom) sends A, X sends Y and Y sends X.",
    "L and R send the triggers (LT/RT) and ZL/ZR send the bumpers, for games "
    "where the triggers are used most.",
    "Both: Xbox face-button positions, and L/R as the triggers."};

const char *preset_label(int p) {
    return (p >= 0 && p < PRESET_COUNT) ? PRESET_LABELS[p] : "Custom";
}
const char *preset_desc(int p) {
    return (p >= 0 && p < PRESET_COUNT)
               ? PRESET_DESCS[p]
               : "Your own mapping. Change individual buttons below, or pick "
                 "a preset here to start over from it.";
}

void apply_preset(PCONFIGURATION cfg, int preset) {
    preset_buttons(std::max(0, std::min(PRESET_COUNT - 1, preset)),
                   cfg->button_map);
}

int matching_preset(const CONFIGURATION *cfg) {
    unsigned char buf[PAD_SOURCE_COUNT];
    for (int p = 0; p < PRESET_COUNT; p++) {
        preset_buttons(p, buf);
        if (memcmp(buf, cfg->button_map, PAD_SOURCE_COUNT) == 0) {
            return p;
        }
    }
    return -1;
}

// ---- config file ------------------------------------------------------------

static int find_target(const std::string &token) {
    for (int t = 0; t < PAD_TARGET_COUNT; t++) {
        if (token == TARGETS[t].token) {
            return t;
        }
    }
    return -1;
}

// Parses "key=value,key=value" with keys from `keys`
static void parse_pairs(const char *value, const char *const *keys,
                        int key_count, unsigned char *out) {
    if (value == NULL) {
        return;
    }
    std::string s(value);
    size_t start = 0;
    while (start < s.size()) {
        size_t comma = s.find(',', start);
        std::string pair = s.substr(start, comma == std::string::npos
                                               ? std::string::npos
                                               : comma - start);
        start = comma == std::string::npos ? s.size() : comma + 1;
        size_t eq = pair.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = pair.substr(0, eq), val = pair.substr(eq + 1);
        auto trim = [](std::string &t) {
            while (!t.empty() && isspace((unsigned char)t.back()))
                t.pop_back();
            while (!t.empty() && isspace((unsigned char)t.front()))
                t.erase(t.begin());
        };
        trim(key);
        trim(val);
        int target = find_target(val);
        if (target < 0) {
            continue;
        }
        for (int k = 0; k < key_count; k++) {
            if (key == keys[k]) {
                out[k] = (unsigned char)target;
            }
        }
    }
}

void parse_button_map(PCONFIGURATION cfg, const char *value) {
    parse_pairs(value, SOURCE_TOKENS, PAD_SOURCE_COUNT, cfg->button_map);
}

void parse_touch_map(PCONFIGURATION cfg, const char *value) {
    parse_pairs(value, ZONE_TOKENS, TOUCH_ZONE_COUNT, cfg->touch_map);
}

static std::string format_pairs(const char *const *keys, int count,
                                const unsigned char *values) {
    std::string out;
    for (int i = 0; i < count; i++) {
        if (i) {
            out += ",";
        }
        out += keys[i];
        out += "=";
        out += target_token(values[i]);
    }
    return out;
}

std::string format_button_map(const CONFIGURATION *cfg) {
    return format_pairs(SOURCE_TOKENS, PAD_SOURCE_COUNT, cfg->button_map);
}

std::string format_touch_map(const CONFIGURATION *cfg) {
    return format_pairs(ZONE_TOKENS, TOUCH_ZONE_COUNT, cfg->touch_map);
}

// ---- runtime ------------------------------------------------------------------

static unsigned char s_buttons[PAD_SOURCE_COUNT];
static unsigned char s_touch[TOUCH_ZONE_COUNT];
static bool s_initialized = false;
static bool s_image_valid = false;

void set_active(const CONFIGURATION *cfg) {
    memcpy(s_buttons, cfg->button_map, PAD_SOURCE_COUNT);
    memcpy(s_touch, cfg->touch_map, TOUCH_ZONE_COUNT);
    s_initialized = true;
    s_image_valid = false;
}

int active_button_target(int source) {
    if (!s_initialized) {
        memcpy(s_buttons, DEFAULT_BUTTONS, PAD_SOURCE_COUNT);
        memcpy(s_touch, DEFAULT_TOUCH, TOUCH_ZONE_COUNT);
        s_initialized = true;
    }
    return (source >= 0 && source < PAD_SOURCE_COUNT) ? s_buttons[source]
                                                       : PAD_NONE;
}

int active_touch_target(int zone) {
    active_button_target(0); // makes sure the defaults are loaded
    return (zone >= 0 && zone < TOUCH_ZONE_COUNT) ? s_touch[zone] : PAD_NONE;
}

void apply_target(int target, int &buttons, uint8_t &lt, uint8_t &rt) {
    switch (target) {
    case PAD_A: buttons |= A_FLAG; break;
    case PAD_B: buttons |= B_FLAG; break;
    case PAD_X: buttons |= X_FLAG; break;
    case PAD_Y: buttons |= Y_FLAG; break;
    case PAD_LB: buttons |= LB_FLAG; break;
    case PAD_RB: buttons |= RB_FLAG; break;
    case PAD_LT: lt = 255; break;
    case PAD_RT: rt = 255; break;
    case PAD_VIEW: buttons |= BACK_FLAG; break;
    case PAD_MENU: buttons |= PLAY_FLAG; break;
    case PAD_GUIDE: buttons |= SPECIAL_FLAG; break;
    case PAD_L3: buttons |= LS_CLK_FLAG; break;
    case PAD_R3: buttons |= RS_CLK_FLAG; break;
    case PAD_L3R3: buttons |= LS_CLK_FLAG | RS_CLK_FLAG; break;
    case PAD_UP: buttons |= UP_FLAG; break;
    case PAD_DOWN: buttons |= DOWN_FLAG; break;
    case PAD_LEFT: buttons |= LEFT_FLAG; break;
    case PAD_RIGHT: buttons |= RIGHT_FLAG; break;
    default: break;
    }
}

// ---- gamepad view picture -----------------------------------------------------

// 5x7 bitmap font for the labels the picture needs
struct Glyph {
    char ch;
    const char *rows[7];
};
static const Glyph FONT[] = {
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'3', {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
};

static const Glyph *find_glyph(char c) {
    for (const auto &g : FONT) {
        if (g.ch == c) {
            return &g;
        }
    }
    return nullptr;
}

static const int IMG_W = 320, IMG_H = 240;
static uint16_t s_image[IMG_W * IMG_H];

// The framebuffer is column-major: screen pixel (x, y) lives at
// x * 240 + (239 - y), the same layout the bundled .bgr pictures use
static void put(int x, int y, uint16_t c) {
    if (x >= 0 && x < IMG_W && y >= 0 && y < IMG_H) {
        s_image[x * IMG_H + (IMG_H - 1 - y)] = c;
    }
}

static void fill(int x0, int y0, int w, int h, uint16_t c) {
    for (int x = x0; x < x0 + w; x++)
        for (int y = y0; y < y0 + h; y++)
            put(x, y, c);
}

static void label(int bx, int by, int bw, int bh, const char *text,
                  uint16_t c) {
    int n = (int)strlen(text);
    if (n == 0) {
        return;
    }
    int scale = std::min(8, std::min((bw - 12) / (n * 6), (bh - 12) / 7));
    scale = std::max(1, scale);
    int tw = (n * 6 - 1) * scale, th = 7 * scale;
    int x0 = bx + (bw - tw) / 2, y0 = by + (bh - th) / 2;
    for (int i = 0; i < n; i++) {
        const Glyph *g = find_glyph(text[i]);
        if (!g) {
            continue;
        }
        for (int r = 0; r < 7; r++)
            for (int col = 0; col < 5; col++)
                if (g->rows[r][col] == '#')
                    fill(x0 + (i * 6 + col) * scale, y0 + r * scale, scale,
                         scale, c);
    }
}

const uint8_t *gamepad_view_image() {
    if (s_image_valid) {
        return (const uint8_t *)s_image;
    }
    active_button_target(0);
    const uint16_t GRAY = 0x7BEF, BLACK = 0x0000, WHITE = 0xFFFF,
                   EMPTY = 0x2104;
    fill(0, 0, IMG_W, IMG_H, GRAY);
    // Same boxes as the zones GamepadTouchHandler reads
    struct Box {
        int x, y, w, h, zone;
    } boxes[TOUCH_ZONE_COUNT] = {{10, 10, 88, 100, ZONE_TOP_LEFT},
                                 {108, 10, 102, 100, ZONE_TOP_MIDDLE},
                                 {220, 10, 90, 100, ZONE_TOP_RIGHT},
                                 {10, 130, 300, 100, ZONE_BOTTOM}};
    for (const auto &b : boxes) {
        int t = s_touch[b.zone];
        fill(b.x, b.y, b.w, b.h, t == PAD_NONE ? EMPTY : BLACK);
        label(b.x, b.y, b.w, b.h, target_short(t), WHITE);
    }
    s_image_valid = true;
    return (const uint8_t *)s_image;
}

} // namespace ButtonMapping
