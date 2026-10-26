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
#include "../system/apt_state.hpp"
#include "../system/dispatcher.hpp"
#include "../system/n3ds_connection.hpp"

#include <3ds.h>
#include <Limelight.h>
#include <cstdio>
#include <string>

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

// Top-screen panel for input-only sessions, drawn with a text console. It is
// redrawn every second (the renderers clear the top screen when the touch
// view changes) and shows a running timer and a blinking light so it is
// obvious the session is alive.
class InputOnlyPanel {
  public:
    explicit InputOnlyPanel(const std::string &host) : host(host) {
        started_ms = osGetTime();
    }

    void update() {
        u64 now = osGetTime();
        if (drawn && now - last_draw_ms < 1000) {
            return;
        }
        last_draw_ms = now;
        // The console needs the plain 400-pixel 2D layout
        bool mode_changed = false;
        if (gfxIs3D()) {
            gfxSet3D(false);
            mode_changed = true;
        }
        if (gfxIsWide()) {
            gfxSetWide(false);
            mode_changed = true;
        }
        if (mode_changed || !drawn) {
            consoleInit(GFX_TOP, &console);
        }
        PrintConsole *prev = consoleSelect(&console);
        draw((now - started_ms) / 1000);
        consoleSelect(prev);
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_TOP, false);
        drawn = true;
    }

  private:
    void draw(u64 seconds) {
        // 50 x 30 characters. CP437 box-drawing characters frame the panel.
        const int L = 4, W = 42;
        auto row = [&](int y, const char *color, const std::string &text) {
            std::string t = text.substr(0, W - 4);
            printf("\x1b[%d;%dH\x1b[37m\xBA\x1b[0m %s%-*s\x1b[0m \x1b[37m\xBA",
                   y, L, color, W - 4, t.c_str());
        };
        std::string bar(W - 2, '\xCD');
        printf("\x1b[0m\x1b[6;%dH\x1b[37m\xC9%s\xBB", L, bar.c_str());
        row(7, "", "");
        row(8, "\x1b[1;37m", "REMOTE INPUT");
        row(9, "\x1b[36m", "Connected to " + host);
        row(10, "", "");
        char status[48];
        snprintf(status, sizeof(status), "Session %llu:%02llu",
                 (unsigned long long)(seconds / 60),
                 (unsigned long long)(seconds % 60));
        // Blinking status light next to the timer
        printf("\x1b[11;%dH\x1b[37m\xBA\x1b[0m %s\xFE\x1b[0m %-*s \x1b[37m\xBA",
               L, (seconds % 2) ? "\x1b[32m" : "\x1b[1;32m", W - 6, status);
        row(12, "", "");
        row(13, "\x1b[37m", "This 3DS is controlling the PC.");
        row(14, "\x1b[37m", "No picture or sound is streamed.");
        row(15, "", "");
        row(16, "\x1b[33m", "Buttons and sticks: game controller");
        row(17, "\x1b[33m", "HOME: menu (mouse, keyboard, touch)");
        row(18, "\x1b[33m", "HOME twice: end the session");
        row(19, "", "");
        printf("\x1b[20;%dH\x1b[37m\xC8%s\xBC\x1b[0m", L, bar.c_str());
    }

    std::string host;
    PrintConsole console;
    u64 started_ms = 0;
    u64 last_draw_ms = 0;
    bool drawn = false;
};

void run_loop(PCONFIGURATION config, N3dsConnectionListener *listener,
              std::shared_ptr<N3dsInput> input_handler,
              const std::string &input_only_host) {
    std::unique_ptr<InputOnlyPanel> panel;
    if (!input_only_host.empty()) {
        panel.reset(new InputOnlyPanel(input_only_host));
    }
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
        if (panel && !apt_state_suspended()) {
            panel->update();
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
