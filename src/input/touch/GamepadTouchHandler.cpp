/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2017 Iwan Timmer
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

#include "../button_mapping.hpp"
#include "TouchHandler.hpp"
#include <Limelight.h>
#include <cstring>

// Five zones, matching the boxes drawn by ButtonMapping::gamepad_view_image():
// three across the top half and two side by side in the bottom half. Each
// sends whatever the touch mapping assigns to it (default L3, L3+R3, R3,
// HOME, Capture).

GamepadTouchHandler::GamepadTouchHandler(GAMEPAD_STATE *gamepad_in)
    : gamepad_state(gamepad_in) {}

GamepadTouchHandler::~GamepadTouchHandler() { release(); }

void GamepadTouchHandler::release() {
    gamepad_state->touch_buttons = 0;
    gamepad_state->touch_left_trigger = 0;
    gamepad_state->touch_right_trigger = 0;
}

void GamepadTouchHandler::press_zone_at(touchPosition touch) {
    int zone;
    if (touch.py >= 120) {
        zone = touch.px < 160 ? ZONE_BOTTOM_LEFT : ZONE_BOTTOM_RIGHT;
    } else if (touch.px < 104) {
        zone = ZONE_TOP_LEFT;
    } else if (touch.px < 215) {
        zone = ZONE_TOP_MIDDLE;
    } else {
        zone = ZONE_TOP_RIGHT;
    }
    int buttons = 0;
    uint8_t lt = 0, rt = 0;
    ButtonMapping::apply_target(ButtonMapping::active_touch_target(zone),
                                buttons, lt, rt);
    gamepad_state->touch_buttons = buttons;
    gamepad_state->touch_left_trigger = lt;
    gamepad_state->touch_right_trigger = rt;
}

void GamepadTouchHandler::_handle_touch_down(touchPosition touch) {
    press_zone_at(touch);
}

void GamepadTouchHandler::_handle_touch_up(touchPosition touch) { release(); }

void GamepadTouchHandler::_handle_touch_hold(touchPosition touch) {
    press_zone_at(touch);
}
