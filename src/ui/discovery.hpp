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

// Host discovery for the 3DS. The desktop clients use mDNS, which the 3DS
// build does not have, so this probes every address of the local subnet
// (at most a /24) for the GameStream HTTP port with non-blocking connects.
#pragma once

#include <string>
#include <vector>

namespace discovery {

// Blocking; run on the worker thread. Returns the dotted addresses that
// accepted a TCP connection on port 47989.
std::vector<std::string> scan_local_network(int timeout_ms = 500,
                                            int concurrency = 16,
                                            int max_total_ms = 15000);

} // namespace discovery
