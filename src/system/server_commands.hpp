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

#include <string>
#include <vector>

// The host's server commands (an Apollo feature): named commands the host
// admin defined, which a client with the Server Command permission can run
// during a stream. The launcher stores the list it got from serverinfo
// before the stream starts; the stream menu's list view reads it.
namespace ServerCommands {

void set(const std::vector<std::string> &names);
const std::vector<std::string> &get();

// Sends the command with this index to the host. Returns false if the
// control stream refused it (not connected).
bool run(int index);

} // namespace ServerCommands
