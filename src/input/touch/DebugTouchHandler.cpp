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
#include "../../system/debug_console.hpp"
#include <Limelight.h>
#include <cstdio>
#include <cstring>

PrintConsole DebugTouchHandler::topScreen;
PrintConsole DebugTouchHandler::bottomScreen;

DebugTouchHandler::DebugTouchHandler() {
    // Status panel on top, scrolling log underneath; stderr goes there too
    DebugConsole::enter();
    consoleDebugInit(debugDevice_CONSOLE);
    DebugConsole::log("Debug view: stats above, logs below\n");
}

DebugTouchHandler::~DebugTouchHandler() {
    consoleDebugInit(debugDevice_NULL);
    DebugConsole::leave();
    consoleSelect(&topScreen);
}

void DebugTouchHandler::_handle_touch_down(touchPosition touch) {}

void DebugTouchHandler::_handle_touch_up(touchPosition touch) {}

void DebugTouchHandler::_handle_touch_hold(touchPosition touch) {}
