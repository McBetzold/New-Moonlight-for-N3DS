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

#include "TouchHandler.hpp"
#include <Limelight.h>

// The 3DS has one touch point; Sunshine identifies it by this pointer id
static const uint32_t POINTER_ID = 1;

PassthroughTouchHandler::PassthroughTouchHandler() {
    supported = (LiGetHostFeatureFlags() & LI_FF_PEN_TOUCH_EVENTS) != 0;
}

PassthroughTouchHandler::~PassthroughTouchHandler() {
    if (touching) {
        if (supported) {
            LiSendTouchEvent(LI_TOUCH_EVENT_CANCEL_ALL, POINTER_ID, 0, 0, 0,
                             0, 0, LI_ROT_UNKNOWN);
        } else {
            LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
        }
    }
}

bool PassthroughTouchHandler::send(uint8_t event_type, touchPosition touch) {
    // Coordinates are fractions of the host's screen; the bottom screen is
    // mapped onto the whole display, like the Mirror view
    float x = touch.px / (float)GSP_SCREEN_HEIGHT_BOTTOM;
    float y = touch.py / (float)GSP_SCREEN_WIDTH;
    int rc = LiSendTouchEvent(event_type, POINTER_ID, x, y, 1.0f, 0.0f, 0.0f,
                              LI_ROT_UNKNOWN);
    if (rc == LI_ERR_UNSUPPORTED) {
        supported = false;
        return false;
    }
    return true;
}

void PassthroughTouchHandler::_handle_touch_down(touchPosition touch) {
    touching = true;
    last_touch = touch;
    if (supported && send(LI_TOUCH_EVENT_DOWN, touch)) {
        return;
    }
    LiSendMousePositionEvent(touch.px, touch.py, GSP_SCREEN_HEIGHT_BOTTOM,
                             GSP_SCREEN_WIDTH);
    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
}

void PassthroughTouchHandler::_handle_touch_hold(touchPosition touch) {
    if (!touching) {
        return;
    }
    last_touch = touch;
    if (supported && send(LI_TOUCH_EVENT_MOVE, touch)) {
        return;
    }
    LiSendMousePositionEvent(touch.px, touch.py, GSP_SCREEN_HEIGHT_BOTTOM,
                             GSP_SCREEN_WIDTH);
}

void PassthroughTouchHandler::_handle_touch_up(touchPosition touch) {
    if (!touching) {
        return;
    }
    touching = false;
    // On the lift frame the touch screen reads (0, 0); lift where the stylus
    // last was so the contact does not jump to the corner
    if (supported && send(LI_TOUCH_EVENT_UP, last_touch)) {
        return;
    }
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
}
