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

#include "../config.hpp"

#include <memory>
#include <string>

class N3dsInput;
class N3dsConnectionListener;

namespace stream_session {

// Spawns the input and message-dispatch threads for an established
// connection and blocks until it closes or the system asks the app to exit.
// A non-empty input_only_host marks an input-only session (Apollo's Remote
// Input): no video arrives, so a status panel naming that host is kept on
// the top screen.
void run_loop(PCONFIGURATION config, N3dsConnectionListener *listener,
              std::shared_ptr<N3dsInput> input_handler,
              const std::string &input_only_host = "");

} // namespace stream_session
