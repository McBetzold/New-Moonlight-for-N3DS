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
#include "../../system/dispatcher.hpp"
#include "TouchHandler.hpp"
#include <Limelight.h>
#include <memory>
#include <vector>

// Four rows of equal height, grouped by purpose; each row splits the width
// evenly between its buttons. Must match 3ds/gfx/menu.png.
//   on-screen controls:          GAMEPAD  KEYBOARD  MOUSE
//   picture on the touch screen: MIRROR   TOUCH       (clicks vs. real touch)
//   other views:                 STRETCH  MAGNIFY
//   tools:                       EXIT     COMMANDS  DEBUG
static const int button_size_y = 60;
static const std::vector<std::vector<N3dsTouchType>> button_rows{
    {N3dsTouchType::GAMEPAD, N3dsTouchType::KEYBOARD,
     N3dsTouchType::MOUSEPAD},
    {N3dsTouchType::ABSOLUTE_TOUCH, N3dsTouchType::PASSTHROUGH_TOUCH},
    {N3dsTouchType::DS_TOUCH, N3dsTouchType::MAGNIFY_TOUCH},
    {N3dsTouchType::DISABLED, N3dsTouchType::SERVER_CMD_TOUCH,
     N3dsTouchType::DEBUG_TOUCH},
};

// HOME stays disallowed while streaming; N3dsInput turns a rejected HOME
// press into "end the stream, then jump to the HOME Menu".
MenuTouchHandler::MenuTouchHandler() { aptSetHomeAllowed(false); }

MenuTouchHandler::~MenuTouchHandler() { aptSetHomeAllowed(false); }

void MenuTouchHandler::_handle_touch_down(touchPosition touch) {
    _handle_touch_hold(touch);
}

void MenuTouchHandler::_handle_touch_up(touchPosition touch) {
    if (message != nullptr)
        MessageDispatcher::get_instance()->post(message);
}

void MenuTouchHandler::_handle_touch_hold(touchPosition touch) {
    touch.py = touch.py < GSP_SCREEN_WIDTH ? touch.py : (touch.py - 1);
    touch.px = touch.px < GSP_SCREEN_HEIGHT_BOTTOM ? touch.px : (touch.px - 1);
    int row = touch.py / button_size_y;
    if (row < 0 || row >= (int)button_rows.size()) {
        message = nullptr;
        return;
    }
    const auto &buttons = button_rows[row];
    int col = touch.px * (int)buttons.size() / GSP_SCREEN_HEIGHT_BOTTOM;
    col = col < 0 ? 0 : col >= (int)buttons.size() ? buttons.size() - 1 : col;

    N3dsTouchType touch_type = buttons[col];
    switch (touch_type) {
    case (N3dsTouchType::GAMEPAD):
        message = std::make_shared<TouchStateChangedMsg>(touch_type);
        break;
    case (N3dsTouchType::MOUSEPAD):
        message = std::make_shared<TouchStateChangedMsg>(touch_type);
        break;
    case (N3dsTouchType::KEYBOARD):
        message = std::make_shared<TouchStateChangedMsg>(touch_type);
        break;
    case (N3dsTouchType::DISABLED):
        // Signal to exit the stream
        message = std::make_shared<GenericEventMsg>(MessageType::EXIT_STREAM);
        break;
    default:
        message = std::make_shared<TouchStateChangedMsg>(touch_type);
        break;
    }
}
