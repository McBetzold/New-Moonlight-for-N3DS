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
#pragma once

#include <3ds.h>

namespace ui {

// Per-frame input snapshot for the launcher UI: buttons with auto-repeat for
// navigation, plus tap/drag classification of the touch screen.
struct Input {
    u32 down = 0;   // newly pressed this frame
    u32 held = 0;   // currently held
    u32 up = 0;     // newly released this frame
    u32 repeat = 0; // newly pressed or auto-repeated
    // Navigation directions (KEY_DUP/KEY_DDOWN/KEY_DLEFT/KEY_DRIGHT) merged
    // from the D-pad and the Circle Pad, with auto-repeat.
    u32 nav = 0;

    bool touch_down = false; // stylus went down this frame
    bool touch_held = false; // stylus is on the screen
    bool touch_up = false;   // stylus lifted this frame
    bool tap = false;        // touch_up after a short, still press
    bool drag = false;       // the current/last touch moved past threshold
    float tx = 0, ty = 0;    // current (or last) touch position
    float start_x = 0, start_y = 0;
    float dx = 0, dy = 0; // movement since the previous frame
    u64 now = 0;          // milliseconds

    bool pressed(u32 key) const { return (down & key) != 0; }
    bool tapped_in(float x, float y, float w, float h) const {
        return tap && tx >= x && tx < x + w && ty >= y && ty < y + h;
    }
    bool touching_in(float x, float y, float w, float h) const {
        return touch_held && tx >= x && tx < x + w && ty >= y && ty < y + h;
    }
};

// Scans HID and fills the snapshot. Call exactly once per frame.
void input_poll(Input &in);
// Forgets the in-progress touch (after a software keyboard or modal).
void input_reset();

} // namespace ui
