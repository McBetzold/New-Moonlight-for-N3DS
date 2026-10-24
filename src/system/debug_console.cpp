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

#include "debug_console.hpp"
#include "ThreadLock.hpp"

#include <3ds.h>
#include <cstdio>
#include <cstring>

namespace {

// The bottom screen is 320x240 and the console font is 8x8: 40 columns by
// 30 rows. libctru's console windows and cursor are 1-based. One column and
// one row are left unused so the layout is also safe on older libctru
// releases that treated window origins as 0-based.
constexpr int COLS = 39;
constexpr int PANEL_X = 1;
constexpr int PANEL_Y = 1;
constexpr int PANEL_ROWS = DebugConsole::STATS_ROWS + 1; // + separator
constexpr int LOG_Y = PANEL_Y + PANEL_ROWS;
constexpr int LOG_ROWS = 25;

PrintConsole stats_console;
PrintConsole log_console;
ThreadLock console_lock;
bool initialized = false;
bool is_active = false;

// Redraws the whole panel: STATS_ROWS text lines padded to the full width,
// then the separator. Clearing with "\x1b[2J" is window-scoped and homes the
// cursor, which avoids depending on cursor-positioning escape semantics.
void draw_panel(const char *text) {
    printf("\x1b[2J");
    const char *p = text;
    for (int row = 0; row < DebugConsole::STATS_ROWS; row++) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len > COLS) {
            len = COLS;
        }
        printf("%-*.*s\n", COLS, len, p);
        p = nl ? nl + 1 : p + strlen(p);
    }
    for (int i = 0; i < COLS; i++) {
        putchar('-');
    }
}

} // namespace

void DebugConsole::enter() {
    console_lock.lock();
    if (!initialized) {
        consoleInit(GFX_BOTTOM, &stats_console);
        consoleSetWindow(&stats_console, PANEL_X, PANEL_Y, COLS, PANEL_ROWS);
        consoleInit(GFX_BOTTOM, &log_console);
        consoleSetWindow(&log_console, PANEL_X, LOG_Y, COLS, LOG_ROWS);
        initialized = true;
    }
    consoleSelect(&stats_console);
    draw_panel("waiting for first frame...");
    consoleSelect(&log_console);
    printf("\x1b[2J");
    is_active = true;
    console_lock.unlock();
}

void DebugConsole::leave() {
    console_lock.lock();
    is_active = false;
    console_lock.unlock();
}

bool DebugConsole::active() {
    console_lock.lock();
    bool result = is_active;
    console_lock.unlock();
    return result;
}

void DebugConsole::set_stats(const char *fmt, ...) {
    char buf[(COLS + 1) * STATS_ROWS + 1];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    console_lock.lock();
    if (is_active) {
        PrintConsole *prev = consoleSelect(&stats_console);
        draw_panel(buf);
        consoleSelect(prev);
    }
    console_lock.unlock();
}

void DebugConsole::vlog(const char *fmt, va_list args) {
    // Collapse runs of identical messages (moonlight-common-c can emit the
    // same warning hundreds of times a second) so they don't bury the rest.
    static char last_line[256];
    static unsigned repeat_count = 0;
    char line[256];
    vsnprintf(line, sizeof(line), fmt, args);

    console_lock.lock();
    if (is_active) {
        PrintConsole *prev = consoleSelect(&log_console);
        if (strcmp(line, last_line) == 0) {
            repeat_count++;
        } else {
            if (repeat_count > 0) {
                printf("  (last message repeated %u more times)\n",
                       repeat_count);
            }
            repeat_count = 0;
            strncpy(last_line, line, sizeof(last_line) - 1);
            last_line[sizeof(last_line) - 1] = '\0';
            fputs(line, stdout);
        }
        consoleSelect(prev);
    } else {
        fputs(line, stdout);
    }
    console_lock.unlock();
}

void DebugConsole::log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(fmt, args);
    va_end(args);
}
