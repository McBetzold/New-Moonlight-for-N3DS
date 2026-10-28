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

// Button mapping: which controller input each 3DS button and each zone of
// the bottom-screen gamepad view sends. Stored in moonlight.conf as
//   button_map = a=a,b=b,x=x,y=y,l=lb,r=rb,zl=lt,zr=rt,start=menu,select=view
//   touch_map = topleft=l3,topmiddle=l3r3,topright=r3,bottomleft=guide,
//               bottomright=capture
#pragma once

#include "../config.hpp"

#include <cstdint>
#include <string>

namespace ButtonMapping {

// ---- names
const char *target_token(int target);  // "lb" (config files)
const char *target_label(int target);  // "LB (left bumper)" (settings)
const char *target_short(int target);  // "LB" (gamepad view picture)
const char *source_label(int source);  // "A button"
const char *zone_label(int zone);      // "Gamepad view: top left"

// ---- defaults and presets
void set_defaults(PCONFIGURATION cfg); // buttons and touch zones
void apply_legacy_swaps(PCONFIGURATION cfg, bool swap_face,
                        bool swap_shoulders);

constexpr int PRESET_COUNT = 4;
const char *preset_label(int preset);
const char *preset_desc(int preset);
void apply_preset(PCONFIGURATION cfg, int preset); // buttons only
// Index of the preset the current button mapping matches, or -1 (custom)
int matching_preset(const CONFIGURATION *cfg);

// ---- config file
void parse_button_map(PCONFIGURATION cfg, const char *value);
void parse_touch_map(PCONFIGURATION cfg, const char *value);
std::string format_button_map(const CONFIGURATION *cfg);
std::string format_touch_map(const CONFIGURATION *cfg);

// ---- runtime (stream)
// Copies the mapping that the input and touch handlers use during a stream
void set_active(const CONFIGURATION *cfg);
int active_button_target(int source);
int active_touch_target(int zone);
// Adds what `target` sends to a controller state
void apply_target(int target, int &buttons, uint8_t &left_trigger,
                  uint8_t &right_trigger);
// Bottom-screen picture of the gamepad view for the active touch mapping,
// in the framebuffer's column-major RGB565 layout (320x240)
const uint8_t *gamepad_view_image();

} // namespace ButtonMapping
