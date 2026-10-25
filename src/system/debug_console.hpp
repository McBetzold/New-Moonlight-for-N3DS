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

#include <cstdarg>

// Bottom-screen console used by the debug view. The screen is split into a
// fixed status panel in the top rows, which is redrawn in place, and a
// scrolling log underneath it. Every call serializes access to libctru's
// console, which has no locking of its own, so it is safe to use from the
// decode thread, the input thread and moonlight-common-c's threads.
namespace DebugConsole {

// Number of text rows in the status panel (a separator row is added below)
constexpr int STATS_ROWS = 4;

// Take over the bottom screen. Called when the debug view is selected.
void enter();
// Stop writing to the panel/log. The caller re-selects its own console.
void leave();
bool active();

// Replace the status panel with up to STATS_ROWS '\n'-separated lines.
void set_stats(const char *fmt, ...);

// Append to the scrolling log. When the debug view is not active this is a
// plain vprintf to whatever console is current.
void log(const char *fmt, ...);
void vlog(const char *fmt, va_list args);

} // namespace DebugConsole
