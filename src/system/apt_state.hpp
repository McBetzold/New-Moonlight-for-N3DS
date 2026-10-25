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

// Tracks whether the system is suspending the app (HOME Menu, sleep) via an
// APT hook so that threads driving the GPU directly can stop before the GPU
// right is taken away.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Installs the APT hook; call once after aptInit().
void apt_state_init(void);
// True between the suspend/sleep notification and the matching restore.
bool apt_state_suspended(void);

#ifdef __cplusplus
}
#endif
