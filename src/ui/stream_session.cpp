/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2019 Iwan Timmer
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

#include "stream_session.hpp"

#include "../input/n3ds_input.hpp"
#include "../system/dispatcher.hpp"
#include "../system/n3ds_connection.hpp"

#include <3ds.h>
#include <Limelight.h>

namespace stream_session {

static void dispatch_loop(void *_unused_) {
    auto pDispatcher = MessageDispatcher::get_instance();
    auto connection_listener = N3dsConnectionListener::get_instance();
    while (connection_listener != nullptr &&
           !connection_listener->is_connection_closed()) {
        gspWaitForAnyEvent();
        pDispatcher->dispatch_all();
    }
}

static void input_loop(void *input_handler_in) {
    N3dsInput *input_handler = static_cast<N3dsInput *>(input_handler_in);
    auto connection_listener = N3dsConnectionListener::get_instance();
    input_handler->force_touchscreen_menu();
    while (connection_listener != nullptr &&
           !connection_listener->is_connection_closed()) {
        gspWaitForAnyEvent();
        input_handler->n3dsinput_handle_event();
    }
}

void run_loop(PCONFIGURATION config, N3dsConnectionListener *listener,
              std::shared_ptr<N3dsInput> input_handler) {
    // Spin off worker threads. They are joined below: both keep raw pointers
    // to the listener and the input handler, which the caller destroys as
    // soon as this function returns.
    size_t stack_size = 0x20000;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    Thread input_thread = nullptr;
    if (!config->viewonly && input_handler != nullptr) {
        input_thread = threadCreate(input_loop, input_handler.get(),
                                    stack_size, priority, -1, false);
    }
    Thread dispatch_thread =
        threadCreate(dispatch_loop, nullptr, stack_size, priority, -1, false);

    // Run the main connection loop
    while (!listener->is_connection_closed() && aptMainLoop()) {
        gspWaitForAnyEvent();
        if (aptShouldClose()) {
            listener->connection_terminated(0);
        }
    }

    // Make sure the helper threads see the connection as closed even if we
    // left the loop because the system asked the app to exit.
    if (!listener->is_connection_closed()) {
        listener->connection_terminated(ML_ERROR_GRACEFUL_TERMINATION);
    }
    if (input_thread) {
        threadJoin(input_thread, U64_MAX);
        threadFree(input_thread);
    }
    if (dispatch_thread) {
        threadJoin(dispatch_thread, U64_MAX);
        threadFree(dispatch_thread);
    }
}

} // namespace stream_session
