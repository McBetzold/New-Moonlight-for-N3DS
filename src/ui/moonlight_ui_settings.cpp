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

// Settings page: grouped like the Moonlight PC client (Basic / Audio / Host /
// Input / Advanced). The list lives on the touch screen; the top screen shows
// the highlighted setting with its description.

#include "moonlight_ui.hpp"

#include "../system/pair_record.hpp"

#include <algorithm>
#include <cstdio>

namespace ui {

static const float TOOLBAR_H = 36.0f;
static const float LIST_X = 6.0f;
static const float LIST_W = BOT_W - 12.0f;
static const float LIST_TOP = TOOLBAR_H + 4.0f;
static const float ROW_H = 34.0f;
static const float HEADER_ROW_H = 24.0f;
static const float SLIDER_ROW_H = 46.0f;

static const int BITRATE_MIN = 500;
static const int BITRATE_MAX = 20000;
static const int BITRATE_STEP = 500;

struct ResolutionPreset {
    int width;
    int height;
    // Display mode asked of the host; 0 = same as the stream. Apollo creates
    // its virtual display at this size and scales the picture down to the
    // stream, so PC games see a normal monitor instead of a tiny one.
    int host_width;
    int host_height;
    const char *label;
    const char *desc; // shown on the top screen while selected
};

static const ResolutionPreset PRESETS[] = {
    {400, 240, 0, 0, "400x240 (Games)",
     "Pixel-exact on the top screen and the lightest to decode, so the "
     "smoothest option. Games draw their own interface, so the small desktop "
     "does not matter."},
    {800, 480, 0, 0, "800x480 (Desktop)",
     "The top screen's shape with four times the pixels, scaled down 2:1. "
     "Best for the Windows desktop and anything with small text."},
    {800, 480, 1920, 1152, "800x480 (PC games, experimental)",
     "For games that won't run below HD. The PC renders on a full 1920x1152 "
     "display and Apollo scales every frame down to the 800x480 stream. "
     "Needs Apollo's virtual display."},
    {640, 480, 0, 0, "640x480 (Bottom-screen views)",
     "Matches the bottom screen's 4:3 shape for the Mirror view. Slightly "
     "squeezed on the top screen."},
    {320, 240, 0, 0, "320x240 (Bottom-screen 1:1)",
     "Pixel-exact on the bottom screen and the lightest of all. The desktop "
     "becomes very small."},
};
static const int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

struct StereoModeOption {
    const char *label;
    const char *desc; // shown on the top screen while selected
};

// Indexed by STEREO_3D_MODE
static const StereoModeOption STEREO_MODES[STEREO_3D_MODE_COUNT] = {
    {"Off", "Plain 2D. The 3D slider does nothing, so nudging it can never "
            "split the picture."},
    {"Side-by-side",
     "For a PC sending half side-by-side 3D: left eye squeezed into the left "
     "half, right eye into the right. With the slider down you should see two "
     "squeezed copies. Use 800x480."},
    {"Flat depth",
     "Works with any stream. Both eyes see the whole picture, set back behind "
     "the screen like a window; the 3D slider sets how far."},
};

static const StereoModeOption &stereo_option(STEREO_3D_MODE mode) {
    int i = (int)mode;
    return STEREO_MODES[(i >= 0 && i < STEREO_3D_MODE_COUNT) ? i : 0];
}

static const char *CUSTOM_RESOLUTION_DESC =
    "Custom size. Width should be a multiple of 400 or 320 and height of "
    "240; nothing larger than 1024x512 can be shown. Press A to change it.";

static bool host_mode_set(const CONFIGURATION *cfg) {
    return cfg->host_width > 0 && cfg->host_height > 0;
}

static int preset_index(const CONFIGURATION *cfg) {
    int hw = host_mode_set(cfg) ? cfg->host_width : 0;
    int hh = host_mode_set(cfg) ? cfg->host_height : 0;
    for (int i = 0; i < PRESET_COUNT; i++) {
        if (PRESETS[i].width == cfg->stream.width &&
            PRESETS[i].height == cfg->stream.height &&
            PRESETS[i].host_width == hw && PRESETS[i].host_height == hh) {
            return i;
        }
    }
    return -1;
}

static std::string size_text(int w, int h) {
    return std::to_string(w) + "x" + std::to_string(h);
}

static std::string bitrate_text(int kbps) {
    char buf[32];
    if (kbps % 1000 == 0) {
        snprintf(buf, sizeof(buf), "%d Mbps", kbps / 1000);
    } else {
        snprintf(buf, sizeof(buf), "%.1f Mbps", kbps / 1000.0);
    }
    return buf;
}

std::string MoonlightUi::resolution_warning() const {
    std::string warning;
    if (config_->stream.width % GSP_SCREEN_HEIGHT_TOP &&
        config_->stream.width % GSP_SCREEN_HEIGHT_BOTTOM) {
        warning += "Width should be a multiple of 400 or 320. ";
    }
    if (config_->stream.height % GSP_SCREEN_WIDTH) {
        warning += "Height should be a multiple of 240. ";
    }
    if (config_->stream.width > 1024 || config_->stream.height > 512) {
        warning += "Streams larger than 1024x512 cannot be displayed. ";
    }
    if (host_mode_set(config_) &&
        config_->host_width * config_->stream.height !=
            config_->host_height * config_->stream.width) {
        warning += "Host display and stream have different shapes; the host "
                   "will add black bars. ";
    }
    return warning;
}

void MoonlightUi::settings_edit_resolution() {
    int w = config_->stream.width, h = config_->stream.height;
    int hw = config_->host_width, hh = config_->host_height;
    if (!prompt_number("Stream width in pixels", w, w)) {
        return;
    }
    if (!prompt_number("Stream height in pixels", h, h)) {
        return;
    }
    if (!prompt_number("Host display width (0 = same as stream)", hw, hw)) {
        return;
    }
    if (hw > 0) {
        // Suggest the height that keeps the stream's shape
        int suggested = hh > 0 ? hh : (w > 0 ? hw * h / w : hw);
        if (!prompt_number("Host display height", suggested, hh)) {
            return;
        }
    }
    config_->stream.width = w;
    config_->stream.height = h;
    if (hw > 0 && hh > 0) {
        config_->host_width = hw;
        config_->host_height = hh;
    } else {
        config_->host_width = 0;
        config_->host_height = 0;
    }
}

void MoonlightUi::settings_build() {
    rows_.clear();
    PCONFIGURATION cfg = config_;

    auto header = [&](const char *label) {
        SettingRow r;
        r.type = SettingRow::Header;
        r.label = label;
        rows_.push_back(r);
    };
    auto toggle = [&](const char *label, const char *desc, bool *field) {
        SettingRow r;
        r.type = SettingRow::Toggle;
        r.label = label;
        r.desc = desc;
        r.is_on = [field] { return *field; };
        r.value = [field] { return *field ? "On" : "Off"; };
        r.adjust = [field](int) { *field = !*field; };
        r.activate = [field] { *field = !*field; };
        rows_.push_back(r);
    };

    header("Basic Settings");
    {
        SettingRow r;
        r.type = SettingRow::Choice;
        r.label = "Resolution";
        r.desc = CUSTOM_RESOLUTION_DESC;
        r.desc_fn = [cfg] {
            int i = preset_index(cfg);
            return std::string(i >= 0 ? PRESETS[i].desc
                                      : CUSTOM_RESOLUTION_DESC);
        };
        r.value = [cfg] {
            int i = preset_index(cfg);
            if (i >= 0) {
                return std::string(PRESETS[i].label);
            }
            std::string v = size_text(cfg->stream.width, cfg->stream.height);
            if (host_mode_set(cfg)) {
                v += ", " + size_text(cfg->host_width, cfg->host_height) +
                     " host";
            }
            return v + " (custom)";
        };
        r.adjust = [cfg](int dir) {
            int i = preset_index(cfg);
            if (i < 0) {
                i = dir > 0 ? 0 : PRESET_COUNT - 1;
            } else {
                i = (i + dir + PRESET_COUNT) % PRESET_COUNT;
            }
            cfg->stream.width = PRESETS[i].width;
            cfg->stream.height = PRESETS[i].height;
            cfg->host_width = PRESETS[i].host_width;
            cfg->host_height = PRESETS[i].host_height;
        };
        r.activate = [this] { settings_edit_resolution(); };
        rows_.push_back(r);
    }
    {
        SettingRow r;
        r.type = SettingRow::Choice;
        r.label = "Frame rate";
        r.desc = "Higher frame rates lower input latency but need more "
                 "bandwidth and decoding power. 60 fps is recommended on the "
                 "New 3DS with hardware decoding. Press A for a custom value.";
        r.value = [cfg] { return std::to_string(cfg->stream.fps) + " fps"; };
        r.adjust = [cfg](int) { cfg->stream.fps = cfg->stream.fps == 60 ? 30 : 60; };
        r.activate = [this, cfg] {
            int fps = cfg->stream.fps;
            if (prompt_number("Frames per second", fps, fps)) {
                cfg->stream.fps = std::max(10, std::min(120, fps));
            }
        };
        rows_.push_back(r);
    }
    {
        SettingRow r;
        r.type = SettingRow::Slider;
        r.label = "Video bitrate";
        r.desc = "Lower the bitrate if you see stuttering or artifacts; raise "
                 "it for a sharper picture on a strong Wi-Fi connection. "
                 "Press A to type an exact value in kbps.";
        r.value = [cfg] { return bitrate_text(cfg->stream.bitrate); };
        r.fraction = [cfg] {
            return (float)(cfg->stream.bitrate - BITRATE_MIN) /
                   (float)(BITRATE_MAX - BITRATE_MIN);
        };
        r.set_fraction = [cfg](float f) {
            f = std::max(0.0f, std::min(1.0f, f));
            int kbps = BITRATE_MIN + (int)(f * (BITRATE_MAX - BITRATE_MIN));
            kbps = (kbps / BITRATE_STEP) * BITRATE_STEP;
            cfg->stream.bitrate = std::max(BITRATE_MIN, std::min(BITRATE_MAX, kbps));
        };
        r.adjust = [cfg](int dir) {
            int kbps = cfg->stream.bitrate + dir * BITRATE_STEP;
            cfg->stream.bitrate = std::max(BITRATE_MIN, std::min(BITRATE_MAX, kbps));
        };
        r.activate = [this, cfg] {
            int kbps = cfg->stream.bitrate;
            if (prompt_number("Bitrate in kbps", kbps, kbps)) {
                cfg->stream.bitrate = std::max(100, std::min(50000, kbps));
            }
        };
        rows_.push_back(r);
    }
    {
        SettingRow r;
        r.type = SettingRow::Choice;
        r.label = "Video decoder";
        r.desc = "Hardware decoding uses the New 3DS video decoder and is the "
                 "only option fast enough for smooth gameplay. Software "
                 "decoding works on the original 3DS but is slow. Disabled "
                 "streams audio only.";
        r.value = [cfg] {
            switch (cfg->video_decoder) {
            case HARDWARE_VIDEO_DECODER:
                return std::string("Hardware (New 3DS)");
            case SOFTWARE_VIDEO_DECODER:
                return std::string("Software (slow)");
            default:
                return std::string("Disabled (audio only)");
            }
        };
        r.adjust = [cfg](int dir) {
            int v = ((int)cfg->video_decoder + dir + 3) % 3;
            cfg->video_decoder = (VIDEO_DECODER_TYPE)v;
        };
        r.activate = [cfg] {
            cfg->video_decoder =
                (VIDEO_DECODER_TYPE)(((int)cfg->video_decoder + 1) % 3);
        };
        rows_.push_back(r);
    }

    header("3D Display");
    {
        SettingRow r;
        r.type = SettingRow::Choice;
        r.label = "3D mode";
        r.desc = STEREO_MODES[STEREO_3D_OFF].desc;
        r.desc_fn = [cfg] {
            return std::string(stereo_option(cfg->stereo_3d).desc);
        };
        r.value = [cfg] {
            return std::string(stereo_option(cfg->stereo_3d).label);
        };
        r.adjust = [cfg](int dir) {
            int v = ((int)cfg->stereo_3d + dir + STEREO_3D_MODE_COUNT) %
                    STEREO_3D_MODE_COUNT;
            cfg->stereo_3d = (STEREO_3D_MODE)v;
        };
        r.activate = [cfg] {
            cfg->stereo_3d = (STEREO_3D_MODE)(((int)cfg->stereo_3d + 1) %
                                              STEREO_3D_MODE_COUNT);
        };
        rows_.push_back(r);
    }
    toggle("Swap eyes",
           "Exchanges the two eye pictures in either 3D mode. It has no "
           "effect while 3D mode is off.",
           &cfg->swap_eyes);
    // Explain what swapping does in the mode that is currently selected
    rows_.back().desc_fn = [cfg] {
        switch (cfg->stereo_3d) {
        case STEREO_3D_SIDE_BY_SIDE:
            return std::string(
                "Gives each eye the other half of the picture. Turn it on if "
                "near things look far away and far things look near.");
        case STEREO_3D_FLAT_DEPTH:
            return std::string("Brings the picture out in front of the screen "
                               "instead of setting it back behind it.");
        default:
            return std::string("Exchanges the two eye pictures in either 3D "
                               "mode. It has no effect while 3D mode is off.");
        }
    };

    header("Audio Settings");
    toggle("Play audio on host PC",
           "Keeps the sound on the PC instead of streaming it to the 3DS.",
           &cfg->localaudio);

    header("Host Settings");
    toggle("Optimize game settings",
           "Lets the host adjust in-game resolution and graphics options to "
           "match the stream (Sunshine: 'Optimal Playable Settings').",
           &cfg->sops);
    toggle("Quit app after stream",
           "Closes the game or app on the PC when the stream ends instead of "
           "leaving it running for a later resume.",
           &cfg->quitappafter);

    header("Input Settings");
    toggle("View-only mode",
           "Shows the stream without sending any button, stick or touch input "
           "to the PC.",
           &cfg->viewonly);
    toggle("Motion controls",
           "Sends the 3DS gyroscope and accelerometer to games that request "
           "motion input.",
           &cfg->motion_controls);
    toggle("Swap A/B and X/Y",
           "Matches the Xbox controller layout so the button positions line up "
           "with on-screen prompts.",
           &cfg->swap_face_buttons);
    toggle("Swap L/R with ZL/ZR",
           "Sends the shoulder buttons as triggers and the triggers as "
           "shoulder buttons.",
           &cfg->swap_triggers_and_shoulders);
    toggle("ZL/ZR as mouse buttons",
           "Uses the triggers as left and right mouse clicks, which pairs well "
           "with the touchpad view.",
           &cfg->use_triggers_for_mouse);

    header("Advanced Settings");
    {
        SettingRow r;
        r.type = SettingRow::Number;
        r.label = "Packet size";
        r.desc = "Maximum size of video packets in bytes. Leave at 1392 unless "
                 "your network needs smaller packets.";
        r.value = [cfg] { return std::to_string(cfg->stream.packetSize); };
        r.activate = [this, cfg] {
            int v = cfg->stream.packetSize;
            if (prompt_number("Packet size in bytes", v, v)) {
                cfg->stream.packetSize = std::max(512, std::min(1472, v));
            }
        };
        rows_.push_back(r);
    }
}

void MoonlightUi::settings_enter() {
    settings_build();
    sel_row_ = 1;
    settings_scroll_ = 0;
    desc_lines_for_ = -1;
    view_ = View::Settings;
}

void MoonlightUi::settings_leave() {
    char *config_file_path = (char *)MOONLIGHT_3DS_PATH "/moonlight.conf";
    config_save(config_file_path, config_);
    toast("Settings saved");
    view_ = cur_host_ ? View::Apps : View::Pcs;
}

float MoonlightUi::settings_row_height(const SettingRow &row) const {
    switch (row.type) {
    case SettingRow::Header:
        return HEADER_ROW_H;
    case SettingRow::Slider:
        return SLIDER_ROW_H;
    default:
        return ROW_H;
    }
}

float MoonlightUi::settings_content_height() const {
    float h = 0;
    for (const auto &r : rows_) {
        h += settings_row_height(r) + 2.0f;
    }
    return h;
}

void MoonlightUi::settings_select(int index) {
    if (rows_.empty()) {
        return;
    }
    index = std::max(0, std::min((int)rows_.size() - 1, index));
    // Skip headers in the direction of travel
    int dir = index >= sel_row_ ? 1 : -1;
    while (index >= 0 && index < (int)rows_.size() &&
           rows_[index].type == SettingRow::Header) {
        index += dir;
    }
    if (index < 0 || index >= (int)rows_.size()) {
        return;
    }
    sel_row_ = index;

    float top = 0;
    for (int i = 0; i < sel_row_; i++) {
        top += settings_row_height(rows_[i]) + 2.0f;
    }
    // Keep the group header visible when moving to the first item of a group
    if (sel_row_ > 0 && rows_[sel_row_ - 1].type == SettingRow::Header) {
        top -= HEADER_ROW_H + 2.0f;
    }
    float bottom = top + settings_row_height(rows_[sel_row_]) +
                   (sel_row_ > 0 && rows_[sel_row_ - 1].type == SettingRow::Header
                        ? HEADER_ROW_H + 2.0f
                        : 0.0f);
    float visible = BOT_H - LIST_TOP - 4.0f;
    if (top < settings_scroll_) {
        settings_scroll_ = top;
    } else if (bottom > settings_scroll_ + visible) {
        settings_scroll_ = bottom - visible;
    }
    float max_scroll = std::max(0.0f, settings_content_height() - visible);
    settings_scroll_ = std::max(0.0f, std::min(settings_scroll_, max_scroll));
}

int MoonlightUi::settings_hit(float tx, float ty, float &row_y) const {
    if (ty < LIST_TOP) {
        return -1;
    }
    float y = LIST_TOP - settings_scroll_;
    for (size_t i = 0; i < rows_.size(); i++) {
        float h = settings_row_height(rows_[i]);
        if (ty >= y && ty < y + h && tx >= LIST_X && tx < LIST_X + LIST_W) {
            row_y = y;
            return (int)i;
        }
        y += h + 2.0f;
    }
    return -1;
}

void MoonlightUi::settings_update(const Input &in) {
    int tb = toolbar_hit(in, 1);
    if (tb == 0 || in.pressed(KEY_B) || in.pressed(KEY_START)) {
        settings_leave();
        return;
    }
    if (rows_.empty()) {
        return;
    }

    if (in.nav & KEY_DUP) {
        settings_select(sel_row_ - 1);
    }
    if (in.nav & KEY_DDOWN) {
        settings_select(sel_row_ + 1);
    }

    SettingRow &row = rows_[sel_row_];
    if ((in.nav & KEY_DLEFT) && row.adjust) {
        row.adjust(-1);
    }
    if ((in.nav & KEY_DRIGHT) && row.adjust) {
        row.adjust(+1);
    }
    if (in.pressed(KEY_A) && row.activate) {
        row.activate();
        return;
    }

    if (in.touch_held && in.drag && !in.touch_down && in.start_y >= TOOLBAR_H) {
        float visible = BOT_H - LIST_TOP - 4.0f;
        float max_scroll = std::max(0.0f, settings_content_height() - visible);
        settings_scroll_ =
            std::max(0.0f, std::min(max_scroll, settings_scroll_ - in.dy));
    }

    if (in.tap) {
        float row_y = 0;
        int hit = settings_hit(in.tx, in.ty, row_y);
        if (hit >= 0 && rows_[hit].type != SettingRow::Header) {
            bool was_selected = hit == sel_row_;
            settings_select(hit);
            SettingRow &r = rows_[hit];
            if (r.type == SettingRow::Slider && r.set_fraction) {
                float track_x = LIST_X + 14.0f;
                float track_w = LIST_W - 28.0f;
                r.set_fraction((in.tx - track_x) / track_w);
            } else if (r.type == SettingRow::Toggle && r.activate) {
                r.activate();
            } else if (r.type == SettingRow::Choice && r.adjust) {
                if (was_selected && in.tx > LIST_X + LIST_W / 2.0f) {
                    r.adjust(+1);
                } else if (was_selected) {
                    r.adjust(-1);
                }
            } else if (r.type == SettingRow::Number && r.activate &&
                       was_selected) {
                r.activate();
                return;
            }
        }
    }
}

void MoonlightUi::settings_draw_top() {
    draw_header("Settings", "", true);

    const float cx = 20.0f, cy = 42.0f, cw = TOP_W - 40.0f, ch = 168.0f;
    rrect(cx, cy, cw, ch, 8, color::surface);

    if (rows_.empty() || sel_row_ >= (int)rows_.size()) {
        return;
    }
    const SettingRow &row = rows_[sel_row_];

    // Group name = nearest header above
    std::string group;
    for (int i = sel_row_; i >= 0; i--) {
        if (rows_[i].type == SettingRow::Header) {
            group = rows_[i].label;
            break;
        }
    }
    float ty = cy + 12.0f;
    text(group, cx + 16.0f, ty, 0.38f, color::skyblue);
    ty += line_height(0.38f) + 2.0f;
    text(row.label, cx + 16.0f, ty, 0.6f, color::text);
    ty += line_height(0.6f) + 2.0f;
    if (row.value) {
        text(row.value(), cx + 16.0f, ty, 0.48f, color::accent_text);
        ty += line_height(0.48f) + 8.0f;
    }
    std::string desc = row.desc_fn ? row.desc_fn() : row.desc;
    if (desc_lines_for_ != sel_row_ || desc_cache_text_ != desc) {
        desc_lines_ = wrap(desc, 0.4f, cw - 32.0f);
        desc_lines_for_ = sel_row_;
        desc_cache_text_ = desc;
    }
    auto lines = desc_lines_;
    if (lines.size() > 4) {
        lines.resize(4);
    }
    ty += text_block(lines, cx + 16.0f, ty, 0.4f, color::text_dim) + 4.0f;

    std::string warning = resolution_warning();
    if (!warning.empty() && (row.label == "Resolution")) {
        auto wl = wrap(warning, 0.38f, cw - 60.0f);
        if (wl.size() > 2) {
            wl.resize(2);
        }
        float wy = std::min(ty, cy + ch - 10.0f - wl.size() * line_height(0.38f));
        icon::warning(cx + 24.0f, wy + 8.0f, 14, color::warn, color::black);
        text_block(wl, cx + 40.0f, wy, 0.38f, color::warn);
    }

    std::vector<std::pair<const char *, std::string>> hints;
    switch (row.type) {
    case SettingRow::Toggle:
        hints.push_back({"A", "Toggle"});
        break;
    case SettingRow::Choice:
        hints.push_back({"<>", "Change"});
        hints.push_back({"A", row.label == "Resolution" || row.label == "Frame rate" ? "Custom" : "Next"});
        break;
    case SettingRow::Slider:
        hints.push_back({"<>", "Adjust"});
        hints.push_back({"A", "Type value"});
        break;
    case SettingRow::Number:
        hints.push_back({"A", "Edit"});
        break;
    default:
        break;
    }
    hints.push_back({"B", "Back & save"});
    draw_hints(hints);
}

void MoonlightUi::settings_draw_bottom() {
    float y = LIST_TOP - settings_scroll_;
    for (size_t i = 0; i < rows_.size(); i++) {
        const SettingRow &row = rows_[i];
        float h = settings_row_height(row);
        if (y + h >= TOOLBAR_H && y <= BOT_H) {
            bool sel = (int)i == sel_row_;
            if (row.type == SettingRow::Header) {
                text(row.label, LIST_X + 8.0f, y + 6.0f, 0.38f, color::skyblue);
            } else {
                rrect(LIST_X, y, LIST_W, h, 5, sel ? color::card_hi : color::card);
                if (sel) {
                    rect(LIST_X, y + 4, 3, h - 8, color::accent);
                }
                float label_y = row.type == SettingRow::Slider
                                    ? y + 4.0f
                                    : y + (h - line_height(0.44f)) / 2.0f;
                text(row.label, LIST_X + 14.0f, label_y, 0.44f, color::text);

                switch (row.type) {
                case SettingRow::Toggle: {
                    bool on = row.is_on && row.is_on();
                    float sw = 34.0f, sh = 18.0f;
                    float sx = LIST_X + LIST_W - sw - 12.0f;
                    float sy = y + (h - sh) / 2.0f;
                    rrect(sx, sy, sw, sh, sh / 2.0f,
                          on ? color::accent : color::border);
                    float kx = on ? sx + sw - sh / 2.0f : sx + sh / 2.0f;
                    circle(kx, sy + sh / 2.0f, sh / 2.0f - 2.0f, color::white);
                    break;
                }
                case SettingRow::Choice:
                case SettingRow::Number: {
                    std::string v = row.value ? row.value() : "";
                    // Use everything right of the label; shrink the font
                    // before resorting to an ellipsis so long preset names
                    // stay readable.
                    float label_w = text_width(row.label, 0.44f);
                    float avail = LIST_W - 14.0f - label_w - 12.0f - 12.0f;
                    if (row.type == SettingRow::Choice) {
                        avail -= 36.0f; // chevrons
                    }
                    float vs = 0.4f;
                    if (text_width(v, vs) > avail) {
                        vs = 0.34f;
                    }
                    v = ellipsize(v, vs, avail);
                    float vx = LIST_X + LIST_W - 12.0f;
                    if (row.type == SettingRow::Choice) {
                        // chevrons
                        float vw = text_width(v, vs);
                        float cyc = y + h / 2.0f;
                        u32 c = sel ? color::text : color::text_faint;
                        triangle(vx - 4, cyc, vx - 10, cyc - 5, vx - 10, cyc + 5, c);
                        triangle(vx - vw - 22, cyc, vx - vw - 16, cyc - 5,
                                 vx - vw - 16, cyc + 5, c);
                        vx -= 14.0f;
                    }
                    text(v, vx, y + (h - line_height(vs)) / 2.0f, vs,
                         sel ? color::accent_text : color::text_dim,
                         C2D_AlignRight);
                    break;
                }
                case SettingRow::Slider: {
                    std::string v = row.value ? row.value() : "";
                    text(v, LIST_X + LIST_W - 12.0f, y + 4.0f, 0.4f,
                         sel ? color::accent_text : color::text_dim,
                         C2D_AlignRight);
                    float track_x = LIST_X + 14.0f;
                    float track_w = LIST_W - 28.0f;
                    float track_y = y + h - 13.0f;
                    float f = row.fraction ? row.fraction() : 0.0f;
                    f = std::max(0.0f, std::min(1.0f, f));
                    rrect(track_x, track_y - 2, track_w, 4, 2, color::border);
                    rrect(track_x, track_y - 2, track_w * f, 4, 2, color::accent);
                    circle(track_x + track_w * f, track_y, 6,
                           sel ? color::accent_text : color::text);
                    break;
                }
                default:
                    break;
                }
            }
        }
        y += h + 2.0f;
    }

    draw_toolbar({{ToolButton::Icon::Back, "Back", true}});
}

} // namespace ui
