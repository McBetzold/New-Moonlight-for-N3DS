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

// Persistent list of host PCs shown on the Computers screen. Stored in
// sd:/3ds/moonlight/hosts.conf; the legacy "paired" address list is imported
// the first time the new file does not exist.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hosts {

struct Record {
    std::string address;
    uint16_t port = 47989;
    std::string name; // hostname reported by the server, may be empty
    std::string mac;  // for Wake-on-LAN, learned while paired; may be empty
};

std::vector<Record> load();
void save(const std::vector<Record> &records);

// Splits "address" or "address:port" typed by the user.
bool split_address(const std::string &input, std::string &address,
                   uint16_t &port);
// Filesystem-safe key for per-host cache directories.
std::string cache_key(const std::string &address, uint16_t port);

} // namespace hosts
