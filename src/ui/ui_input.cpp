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

#include "ui_input.hpp"

#include <cmath>

namespace ui {

static bool s_touch_was_held = false;
static float s_prev_x = 0, s_prev_y = 0;
static float s_start_x = 0, s_start_y = 0;
static u64 s_touch_start_ms = 0;
static bool s_dragging = false;
static bool s_repeat_configured = false;

static const float TAP_MOVE_LIMIT = 10.0f;
static const u64 TAP_TIME_LIMIT_MS = 600;

void input_reset() {
    s_touch_was_held = false;
    s_dragging = false;
}

void input_poll(Input &in) {
    if (!s_repeat_configured) {
        // delay / interval in frames
        hidSetRepeatParameters(18, 5);
        s_repeat_configured = true;
    }

    hidScanInput();
    in.now = osGetTime();
    in.down = hidKeysDown();
    in.held = hidKeysHeld();
    in.up = hidKeysUp();
    in.repeat = hidKeysDownRepeat();

    // KEY_UP etc. already merge the D-pad and Circle Pad in libctru.
    in.nav = 0;
    if (in.repeat & KEY_UP)
        in.nav |= KEY_DUP;
    if (in.repeat & KEY_DOWN)
        in.nav |= KEY_DDOWN;
    if (in.repeat & KEY_LEFT)
        in.nav |= KEY_DLEFT;
    if (in.repeat & KEY_RIGHT)
        in.nav |= KEY_DRIGHT;

    bool held_now = (in.held & KEY_TOUCH) != 0;
    in.touch_down = false;
    in.touch_up = false;
    in.tap = false;
    in.dx = in.dy = 0;

    if (held_now) {
        touchPosition pos;
        hidTouchRead(&pos);
        in.tx = pos.px;
        in.ty = pos.py;
        if (!s_touch_was_held) {
            in.touch_down = true;
            s_start_x = s_prev_x = in.tx;
            s_start_y = s_prev_y = in.ty;
            s_touch_start_ms = in.now;
            s_dragging = false;
        } else {
            in.dx = in.tx - s_prev_x;
            in.dy = in.ty - s_prev_y;
            s_prev_x = in.tx;
            s_prev_y = in.ty;
            if (!s_dragging &&
                (std::fabs(in.tx - s_start_x) > TAP_MOVE_LIMIT ||
                 std::fabs(in.ty - s_start_y) > TAP_MOVE_LIMIT)) {
                s_dragging = true;
            }
        }
    } else if (s_touch_was_held) {
        in.touch_up = true;
        in.tx = s_prev_x;
        in.ty = s_prev_y;
        in.tap = !s_dragging && (in.now - s_touch_start_ms) < TAP_TIME_LIMIT_MS;
    }

    in.touch_held = held_now;
    in.start_x = s_start_x;
    in.start_y = s_start_y;
    in.drag = s_dragging && (held_now || in.touch_up);
    s_touch_was_held = held_now;
}

} // namespace ui
