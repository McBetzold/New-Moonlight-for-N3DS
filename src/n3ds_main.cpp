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

#include "config.hpp"
#include "input/touch/TouchHandler.hpp"
#include "system/apt_state.hpp"
#include "ui/moonlight_ui.hpp"

#include <3ds.h>
#include <client.h>

#include <exception>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#define SOC_ALIGN 0x1000
// 0x40000 for each enet host (2 hosts total)
// 0x40000 for each platform socket (2 sockets total)
#define SOC_BUFFERSIZE 0x100000

static u32 *SOC_buffer = NULL;

static void wait_for_button(const char *prompt) {
    printf("\n%s\n", prompt);
    while (aptMainLoop()) {
        gfxSwapBuffers();
        gfxFlushBuffers();
        gspWaitForVBlank();

        hidScanInput();
        if (hidKeysDown())
            break;
    }
}

static void n3ds_exit_handler(void) {
    NDMU_UnlockState();
    NDMU_LeaveExclusiveState();
    ndmuExit();
    irrstExit();
    hidExit();
    SOCU_ShutdownSockets();
    SOCU_CloseSockets();
    socExit();
    free(SOC_buffer);
    romfsExit();
    aptExit();
    gfxExit();
    acExit();
}

static void init_3ds() {
    Result status = 0;
    acInit();
    gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
    gfxSetDoubleBuffering(GFX_TOP, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);

    // The consoles are only used while streaming (debug view, stream status
    // messages); the launcher UI renders with citro2d.
    consoleInit(GFX_TOP, &DebugTouchHandler::topScreen);
    consoleInit(GFX_BOTTOM, &DebugTouchHandler::bottomScreen);
    consoleSelect(&DebugTouchHandler::topScreen);
    atexit(n3ds_exit_handler);

    osSetSpeedupEnable(true);
    aptSetSleepAllowed(false);
    aptInit();
    apt_state_init();
    hidInit();

    SOC_buffer = (u32 *)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    status = socInit(SOC_buffer, SOC_BUFFERSIZE);
    if (R_FAILED(status)) {
        printf("socInit: %08lX\n", status);
        wait_for_button("Press any button to exit");
        exit(1);
    }

    status = ndmuInit();
    status |= NDMU_EnterExclusiveState(NDM_EXCLUSIVE_STATE_INFRASTRUCTURE);
    status |= NDMU_LockState();
    if (R_FAILED(status)) {
        printf("Warning: failed to enter exclusive NDM state: %08lX\n", status);
        wait_for_button("Press any button to continue");
    }
}

static int main_loop(int argc, char *argv[]) {
    init_3ds();

    CONFIGURATION config;
    config_parse(argc, argv, &config);

    // The name hosts show for this client (Apollo keys per-client settings
    // on it; it used to be moonlight-embedded's placeholder "roth")
    {
        bool is_new_3ds = false;
        APT_CheckNew3DS(&is_new_3ds);
        gs_set_device_name(is_new_3ds ? "New 3DS" : "3DS");
    }

    {
        ui::MoonlightUi app(&config);
        app.run();
    }
    return 0;
}

int main(int argc, char *argv[]) {
    try {
        main_loop(argc, argv);
    } catch (const std::exception &ex) {
        printf("Moonlight crashed with the following error: %s\n", ex.what());
        wait_for_button("Press any button to exit");
        return 1;
    } catch (const std::string &ex) {
        printf("Moonlight crashed with the following error message: %s\n",
               ex.c_str());
        wait_for_button("Press any button to exit");
        return 1;
    } catch (...) {
        printf("Moonlight crashed with an unknown error\n");
        wait_for_button("Press any button to exit");
        return 1;
    }
    return 0;
}
