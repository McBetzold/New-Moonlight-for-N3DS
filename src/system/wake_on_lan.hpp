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

namespace wol {

// True when `mac` looks like "aa:bb:cc:dd:ee:ff" (also accepts '-' and no
// separators).
bool valid_mac(const std::string &mac);

// Sends Wake-on-LAN magic packets for `mac`: as a broadcast on the local
// network and directly to `address` (useful when the PC's IP is static and
// its network card still answers ARP). Returns true if at least one packet
// went out.
bool send(const std::string &mac, const std::string &address);

} // namespace wol
