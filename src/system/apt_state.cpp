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

#include "apt_state.hpp"

#include <3ds.h>

static aptHookCookie s_cookie;
static volatile bool s_suspended = false;
static bool s_installed = false;

static void apt_hook(APT_HookType hook, void *param) {
    switch (hook) {
    case APTHOOK_ONSUSPEND:
    case APTHOOK_ONSLEEP:
        s_suspended = true;
        break;
    case APTHOOK_ONRESTORE:
    case APTHOOK_ONWAKEUP:
        s_suspended = false;
        break;
    default:
        break;
    }
}

void apt_state_init(void) {
    if (s_installed) {
        return;
    }
    aptHook(&s_cookie, apt_hook, NULL);
    s_installed = true;
}

bool apt_state_suspended(void) { return s_suspended; }

static volatile bool s_home_requested = false;

void apt_state_request_home(void) { s_home_requested = true; }

bool apt_state_take_home_request(void) {
    bool requested = s_home_requested;
    s_home_requested = false;
    return requested;
}
