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

#include "TouchHandler.hpp"
#include <Limelight.h>

// The Stretch view shows the central 320/400 of the lower half's width on
// the bottom screen (see N3dsRendererDualScreenStretch), so a touch at the
// left edge of the bottom screen is 10% in from the left of the stream.
// Positions are sent relative to a 320x480 reference box.
static const int REF_W = GSP_SCREEN_HEIGHT_BOTTOM;
static const int REF_H = 2 * GSP_SCREEN_WIDTH;
static const int CROP_W = REF_W * GSP_SCREEN_HEIGHT_BOTTOM / GSP_SCREEN_HEIGHT_TOP;
static const int CROP_X = (REF_W - CROP_W) / 2;

static inline void send_position(touchPosition touch) {
    int x = CROP_X + (int)touch.px * CROP_W / GSP_SCREEN_HEIGHT_BOTTOM;
    int y = (int)touch.py + GSP_SCREEN_WIDTH;
    LiSendMousePositionEvent((short)x, (short)y, (short)REF_W, (short)REF_H);
}

StretchTouchHandler::StretchTouchHandler() {}

void StretchTouchHandler::_handle_touch_down(touchPosition touch) {
    send_position(touch);
    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
}

void StretchTouchHandler::_handle_touch_up(touchPosition touch) {
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
}

void StretchTouchHandler::_handle_touch_hold(touchPosition touch) {
    send_position(touch);
}
