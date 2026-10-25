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

#include "server_commands.hpp"

#include <Limelight.h>

namespace ServerCommands {

static std::vector<std::string> s_names;

void set(const std::vector<std::string> &names) { s_names = names; }

const std::vector<std::string> &get() { return s_names; }

bool run(int index) {
    if (index < 0 || index >= (int)s_names.size() || index > 255) {
        return false;
    }
    return LiSendExecServerCmd((uint8_t)index) == 0;
}

} // namespace ServerCommands
