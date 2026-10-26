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

// Server commands view: a text console on the bottom screen listing the
// host's commands two text rows (16 px) apart so they are easy to tap.

#include "../../system/dispatcher.hpp"
#include "../../system/server_commands.hpp"
#include "TouchHandler.hpp"

#include <3ds.h>
#include <cstdio>
#include <cstring>

// Console rows are 8 px; each list entry takes two of them
static const int ROW_PX = 16;
static const int FIRST_ENTRY_ROW = 1;           // in 16 px rows
static const int MAX_ENTRIES = 11;              // rows 1..11
static const int BACK_ROW = MAX_ENTRIES + 1;    // row 12
static const int STATUS_ROW = MAX_ENTRIES + 2;  // row 13 (y 208)

ServerCmdTouchHandler::ServerCmdTouchHandler() {
    consoleInit(GFX_BOTTOM, &console);
    draw();
}

ServerCmdTouchHandler::~ServerCmdTouchHandler() {
    consoleSelect(&DebugTouchHandler::topScreen);
}

// libctru's console rows are 1-based and 8 px tall. A 16 px touch zone z
// starts at y = 16 * z, which is console row 2 * z + 1; text is printed
// there so it sits inside the zone that taps on it select.
static int zone_line(int zone) { return 2 * zone + 1; }

void ServerCmdTouchHandler::draw() {
    consoleSelect(&console);
    consoleClear();
    const auto &names = ServerCommands::get();
    if ((int)names.size() > MAX_ENTRIES) {
        printf("\x1b[%d;1H\x1b[1;37m Server commands \x1b[0;90m(%d more not "
               "shown)\x1b[0m",
               zone_line(0), (int)names.size() - MAX_ENTRIES);
    } else {
        printf("\x1b[%d;1H\x1b[1;37m Server commands\x1b[0m", zone_line(0));
    }
    if (names.empty()) {
        printf("\x1b[%d;1H\x1b[37m No server commands.\n\n"
               " Define them in Apollo's web UI\n"
               " (Server Commands) and give this\n"
               " 3DS the \"Server Command\"\n"
               " permission in the PIN tab.\x1b[0m",
               zone_line(1));
    }
    int shown = (int)names.size() < MAX_ENTRIES ? (int)names.size()
                                                : MAX_ENTRIES;
    for (int i = 0; i < shown; i++) {
        // 40 columns: "  N. " + up to 33 characters of name
        char line[41];
        snprintf(line, sizeof(line), " %2d. %-33.33s", i + 1,
                 names[i].c_str());
        bool hi = i == pressed_row;
        printf("\x1b[%d;1H%s%s\x1b[0m", zone_line(FIRST_ENTRY_ROW + i),
               hi ? "\x1b[30;47m" : "\x1b[37m", line);
    }
    printf("\x1b[%d;1H%s %-38s\x1b[0m", zone_line(BACK_ROW),
           pressed_row == BACK_ROW ? "\x1b[30;47m" : "\x1b[36m",
           "<  Back to menu");
    if (status[0]) {
        printf("\x1b[%d;1H\x1b[32m %-38s\x1b[0m", zone_line(STATUS_ROW),
               status);
    }
}

void ServerCmdTouchHandler::tick() {
    // The renderer that owned the bottom screen before this view may clear
    // it a moment after the console is drawn; draw once more shortly after.
    if (!redrawn && ++idle_events > 30) {
        redrawn = true;
        draw();
    }
}

int ServerCmdTouchHandler::row_at(touchPosition touch) const {
    int row = touch.py / ROW_PX;
    const auto &names = ServerCommands::get();
    if (row == BACK_ROW) {
        return BACK_ROW;
    }
    int idx = row - FIRST_ENTRY_ROW;
    if (idx >= 0 && idx < (int)names.size() && idx < MAX_ENTRIES) {
        return idx;
    }
    return -1;
}

void ServerCmdTouchHandler::_handle_touch_down(touchPosition touch) {
    pressed_row = row_at(touch);
    draw();
}

void ServerCmdTouchHandler::_handle_touch_hold(touchPosition touch) {}

void ServerCmdTouchHandler::_handle_touch_up(touchPosition touch) {
    int row = pressed_row;
    pressed_row = -1;
    if (row == BACK_ROW) {
        MessageDispatcher::get_instance()->post(
            std::make_shared<TouchStateChangedMsg>(N3dsTouchType::MENU_TOUCH));
        return;
    }
    if (row >= 0) {
        const auto &names = ServerCommands::get();
        if (ServerCommands::run(row)) {
            snprintf(status, sizeof(status), "Sent: %.30s",
                     names[row].c_str());
        } else {
            snprintf(status, sizeof(status), "Could not send %.26s",
                     names[row].c_str());
        }
    }
    draw();
}
