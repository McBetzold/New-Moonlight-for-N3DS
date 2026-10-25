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

// Launching an app and running the stream. The connection is brought up on
// the worker thread while the UI shows stage progress (like the PC client's
// "Starting ..." segue). Right before moonlight-common-c creates the video
// decoder the UI releases the GPU, the stream runs with the existing
// renderers, and the UI comes back when the connection ends.

#include "moonlight_ui.hpp"

#include "../audio/audio.h"
#include "../input/n3ds_input.hpp"
#include "../input/touch/TouchHandler.hpp"
#include "../system/apt_state.hpp"
#include "../system/n3ds_connection.hpp"
#include "../system/server_commands.hpp"
#include "../video/video.hpp"
#include "stream_session.hpp"

#include <Limelight.h>
#include <errors.h>
#include <http.h>

#include <cstdio>
#include <cstring>

namespace ui {

static std::string start_app_error(int status, const std::string &gs_err,
                                   const CONFIGURATION &cfg) {
    const STREAM_CONFIGURATION &stream = cfg.stream;
    // The host judges the display mode it was asked for, which is the host
    // display mode when one is set and the stream size otherwise.
    bool host_mode = cfg.host_width > 0 && cfg.host_height > 0;
    int mode_w = host_mode ? cfg.host_width : stream.width;
    int mode_h = host_mode ? cfg.host_height : stream.height;
    switch (status) {
    case GS_NOT_SUPPORTED_4K:
        return "The host does not support 4K streaming.";
    case GS_NOT_SUPPORTED_MODE:
        return "The host does not support " + std::to_string(mode_w) + "x" +
               std::to_string(mode_h) + " at " + std::to_string(stream.fps) +
               " fps.";
    case GS_NOT_SUPPORTED_SOPS_RESOLUTION:
        return "Optimal Playable Settings are not supported for " +
               std::to_string(mode_w) + "x" + std::to_string(mode_h) +
               ". Use a supported resolution or turn off 'Optimize game "
               "settings'.";
    case GS_ERROR:
        return gs_err.empty() ? "The host reported an error." : gs_err;
    default:
        return "Error code " + std::to_string(status) +
               (gs_err.empty() ? "" : " (" + gs_err + ")");
    }
}

void MoonlightUi::launch_app(int index) {
    if (!cur_host_ || index < 0 || index >= (int)apps_.size()) {
        return;
    }
    auto host = cur_host_;
    AppEntry app = apps_[index]; // copy: apps_ may be rebuilt afterwards
    app.art = boxart::Texture(); // the copy must not own the texture

    if (host->current_game != 0 && host->current_game != app.id) {
        if (!confirm("Quit " + running_app_name() + "?",
                     running_app_name() + " is running on " +
                         host->display_name() + ". Quit it and launch " +
                         app.name + "?",
                     "Quit and launch", "Cancel", true)) {
            return;
        }
        if (!quit_running_app(host)) {
            return;
        }
    }

    // Apollo refuses the launch outright without this permission; explain it
    // instead of showing its bare "Permission denied".
    if (host->server_valid && host->server.isApollo &&
        !(host->server.permission & APOLLO_PERM_LAUNCH)) {
        show_error("Not allowed to launch apps",
                   "Apollo gives new clients view-only access. On the PIN "
                   "tab of Apollo's web UI, find \"New 3DS\" and enable "
                   "Launch Apps, plus the controller, mouse and keyboard "
                   "input permissions you want.");
        return;
    }

    config_->stream.supportedVideoFormats = VIDEO_FORMAT_H264;

    // Phase 1: ask the host to launch (or resume) the app.
    auto status = std::make_shared<int>(GS_FAILED);
    auto error = std::make_shared<std::string>();
    PCONFIGURATION cfg = config_;
    int app_id = app.id;
    std::string app_uuid = app.uuid;
    auto job = Worker::instance().post([host, cfg, app_id, app_uuid, status,
                                        error] {
        http_set_timeout_s(30);
        gs_set_host_display_mode(cfg->host_width, cfg->host_height);
        *status = gs_start_app(&host->server, &cfg->stream, app_id,
                               app_uuid.c_str(), cfg->sops, cfg->localaudio,
                               1);
        http_set_timeout_s(60);
        if (*status != GS_OK) {
            *error = gs_error ? gs_error : "";
        }
    });

    Modal m;
    m.title = "Starting " + app.name;
    m.busy_text = (host->current_game == app.id ? "Resuming on "
                                                : "Launching on ") +
                  host->display_name();
    if (!wait_job(job, m)) {
        return;
    }
    if (*status != GS_OK) {
        show_error("Unable to start " + app.name,
                   start_app_error(*status, *error, *config_));
        return;
    }
    host->current_game = app.id;

    // Phase 2: connect and stream.
    run_stream(host, app);
}

void MoonlightUi::run_stream(const std::shared_ptr<HostEntry> &host,
                             const AppEntry &app) {
    PCONFIGURATION cfg = config_;

    // Release texture memory before the decoder allocates its buffers.
    Worker::instance().drop_background();
    free_boxart();

    std::shared_ptr<N3dsInput> input_handler = nullptr;
    if (!cfg->viewonly) {
        input_handler = std::make_shared<N3dsInput>(
            cfg->stream.width, cfg->stream.height, cfg->swap_face_buttons,
            cfg->swap_triggers_and_shoulders, cfg->use_triggers_for_mouse,
            cfg->motion_controls);
    }

    AUDIO_RENDERER_CALLBACKS *audio_callbacks =
        cfg->localaudio ? &audio_callbacks_mock : &audio_callbacks_n3ds;
    PDECODER_RENDERER_CALLBACKS video_callbacks = &decoder_callbacks_mock;
    switch (cfg->video_decoder) {
    case VIDEO_DECODER_TYPE::HARDWARE_VIDEO_DECODER:
        video_callbacks = &decoder_callbacks_n3ds_mvd;
        break;
    case VIDEO_DECODER_TYPE::SOFTWARE_VIDEO_DECODER:
        video_callbacks = &decoder_callbacks_n3ds;
        break;
    default:
        break;
    }
    // Read by the top-screen renderer, which is created once video starts
    N3dsRendererBase::set_stereo_options(cfg->stereo_3d, cfg->swap_eyes);
    // Apollo's server commands, shown by the stream menu's Commands view
    {
        std::vector<std::string> cmds;
        for (int i = 0; i < host->server.serverCmdCount; i++) {
            if (host->server.serverCmds[i]) {
                cmds.push_back(host->server.serverCmds[i]);
            }
        }
        ServerCommands::set(cmds);
    }

    N3dsConnectionListener *listener =
        N3dsConnectionListener::create_instance(cfg->motion_controls);
    // The video decoder is created when this stage starts; the UI must have
    // let go of the GPU by then.
    listener->arm_ui_handshake(STAGE_VIDEO_STREAM_START);

    // moonlight-common-c creates its threads with the priority of the thread
    // calling LiStartConnection. The worker runs slightly below the UI
    // thread, which starved the control/async-callback threads; run the
    // connection setup at the main thread's priority instead.
    s32 main_priority = 0x30;
    svcGetThreadPriority(&main_priority, CUR_THREAD_HANDLE);

    auto status = std::make_shared<int>(0);
    auto job = Worker::instance().post([host, cfg, video_callbacks,
                                        audio_callbacks, status,
                                        main_priority] {
        s32 old_priority = main_priority;
        svcGetThreadPriority(&old_priority, CUR_THREAD_HANDLE);
        svcSetThreadPriority(CUR_THREAD_HANDLE, main_priority);
        *status = LiStartConnection(&host->server.serverInfo, &cfg->stream,
                                    &n3ds_connection_callbacks, video_callbacks,
                                    audio_callbacks, NULL, DISPLAY_FULLSCREEN,
                                    cfg->audio_device, 0);
        svcSetThreadPriority(CUR_THREAD_HANDLE, old_priority);
    });

    Modal m;
    m.title = "Starting " + app.name;
    m.busy = true;
    bool ui_alive = true;
    int last_stage = -1;
    while (!job->is_done()) {
        if (listener->ui_teardown_requested()) {
            m.busy_text = "Starting video stream";
            render(&m);
            // Leave the last frame on screen until the first video frame
            // replaces it.
            gfx_fini();
            ui_alive = false;
            listener->ui_teardown_ack();
            break;
        }
        int stage = listener->current_stage();
        if (stage != last_stage) {
            const char *name = LiGetStageName(stage);
            m.busy_text = stage == STAGE_NONE
                              ? "Connecting to " + host->display_name()
                              : std::string(name ? name : "Connecting");
            last_stage = stage;
        }
        if (!aptMainLoop()) {
            quit_ = true;
        }
        if (!quit_) {
            input_poll(in_);
            now_ = in_.now;
            render(&m);
        } else {
            svcSleepThread(5 * 1000 * 1000);
        }
    }
    while (!job->is_done()) {
        svcSleepThread(5 * 1000 * 1000);
    }

    int connect_status = *status;
    std::string failure;
    if (connect_status != 0) {
        const char *stage_name = LiGetStageName(listener->failed_stage());
        failure = std::string("Stage '") + (stage_name ? stage_name : "?") +
                  "' failed with error " + std::to_string(connect_status) +
                  ".";
        n3ds_connection_callbacks.connectionTerminated(connect_status);
    } else {
        stream_session::run_loop(cfg, listener, input_handler);
    }

    LiStopConnection();

    int term_code = listener->termination_code();
    bool terminated = listener->has_terminated();
    std::string term_message = listener->termination_message();
    N3dsConnectionListener::destroy_instance();
    input_handler = nullptr;

    // The renderers may have re-allocated the framebuffers (wide mode); point
    // the consoles at the current ones and bring the UI back.
    consoleInit(GFX_TOP, &DebugTouchHandler::topScreen);
    consoleInit(GFX_BOTTOM, &DebugTouchHandler::bottomScreen);
    consoleSelect(&DebugTouchHandler::topScreen);
    if (!ui_alive) {
        gfx_init();
    }
    input_reset();

    if (quit_) {
        return;
    }

    if (apt_state_take_home_request()) {
        // HOME was pressed during the stream: show the launcher, then do the
        // jump the input thread deferred. Returns when the user comes back.
        render(nullptr);
        aptJumpToHomeMenu();
        input_reset();
    }

    if (connect_status == 0 && cfg->quitappafter) {
        quit_running_app(host);
    }

    if (connect_status != 0) {
        show_error("Unable to start the stream",
                   failure + "\nCheck that the host is reachable and that the "
                             "stream settings are supported.");
    } else if (terminated && term_code != ML_ERROR_GRACEFUL_TERMINATION) {
        show_error("Connection lost", term_message);
    }

    after_stream(host);
}

void MoonlightUi::after_stream(const std::shared_ptr<HostEntry> &host) {
    // Refresh the running-app state and the list; box art reloads from the
    // SD cache.
    probe_host(host, true);
    if (view_ == View::Apps && cur_host_ == host) {
        requeue_boxart();
        load_apps(host, false);
    }
}

} // namespace ui
