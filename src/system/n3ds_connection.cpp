/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2017 Iwan Timmer
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

#include "n3ds_connection.hpp"
#include "../input/n3ds_input.hpp"
#include "../system/debug_console.hpp"
#include "../system/dispatcher.hpp"

#include <3ds.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>

std::unique_ptr<N3dsConnectionListener> N3dsConnectionListener::instance =
    nullptr;

void N3dsConnectionListener::connection_terminated(int errorCode) {
    char buffer[96];
    const char *message;
    switch (errorCode) {
    case ML_ERROR_GRACEFUL_TERMINATION:
        message = "Connection has been terminated gracefully.";
        break;
    case ML_ERROR_NO_VIDEO_TRAFFIC:
        message = "No video received from host. Check the host PC's firewall "
                  "and port forwarding rules.";
        break;
    case ML_ERROR_NO_VIDEO_FRAME:
        message = "Your network connection isn't performing well. Reduce your "
                  "video bitrate setting or try a faster connection.";
        break;
    case ML_ERROR_UNEXPECTED_EARLY_TERMINATION:
        message = "The connection was unexpectedly terminated by the host due "
                  "to a video capture error. Make sure no DRM-protected "
                  "content is playing on the host.";
        break;
    case ML_ERROR_PROTECTED_CONTENT:
        message = "The connection was terminated by the host due to "
                  "DRM-protected content. Close any DRM-protected content on "
                  "the host and try again.";
        break;
    default:
        snprintf(buffer, sizeof(buffer),
                 "Connection terminated with error: %d", errorCode);
        message = buffer;
        break;
    }
    printf("%s\n", message);

    // Keep the first report; the stream menu's exit request and the library
    // may both end up here.
    if (!terminated.load()) {
        message_lock.lock();
        term_message = message;
        message_lock.unlock();
        term_code.store(errorCode);
        terminated.store(true);
    }

    if (enable_motion) {
        HIDUSER_DisableAccelerometer();
        HIDUSER_DisableGyroscope();
    }
    connection_closed.store(true);
}

void N3dsConnectionListener::connection_log_message(const char *format,
                                                    va_list arglist) {
    if (!debug.load()) {
        return;
    }
    // Goes to the scrolling log region of the debug view
    DebugConsole::vlog(format, arglist);
}

void N3dsConnectionListener::connection_status_update(int status) {
    // Drives the warning marker on the top screen
    connection_poor.store(status == CONN_STATUS_POOR);

    if (!debug.load()) {
        return;
    }

    switch (status) {
    case CONN_STATUS_OKAY:
        printf("Connection is okay\n");
        break;
    case CONN_STATUS_POOR:
        printf("Connection is poor\n");
        break;
    }
}

bool N3dsConnectionListener::connection_is_poor() {
    return connection_poor.load();
}

void N3dsConnectionListener::set_motion_event_state(
    unsigned short controllerNumber, unsigned char motionType,
    unsigned short reportRateHz) {
    if (!enable_motion) {
        return;
    }

    switch (motionType) {
    case LI_MOTION_TYPE_ACCEL:
        if (reportRateHz > 0) {
            HIDUSER_EnableAccelerometer();
            // Alert the input handler
            auto pDispatcher = MessageDispatcher::get_instance();
            auto msg = std::make_shared<GenericEventMsg>(ENABLE_ACCEL);
            pDispatcher->post(msg);
        } else {
            HIDUSER_DisableAccelerometer();
        }
        break;
    case LI_MOTION_TYPE_GYRO:
        if (reportRateHz > 0) {
            HIDUSER_EnableGyroscope();
            // Alert the input handler
            auto pDispatcher = MessageDispatcher::get_instance();
            auto msg = std::make_shared<GenericEventMsg>(ENABLE_GYRO);
            pDispatcher->post(msg);
        } else {
            HIDUSER_DisableGyroscope();
        }
        break;
    }
}

void N3dsConnectionListener::stage_starting(int stage_in) {
    stage.store(stage_in);
    if (handshake_armed && stage_in == handshake_stage) {
        handshake_armed = false;
        LightEvent_Signal(&teardown_request);
        LightEvent_Wait(&teardown_done);
    }
}

void N3dsConnectionListener::stage_complete(int stage_in) {}

void N3dsConnectionListener::stage_failed(int stage_in, int errorCode) {
    failed_stage_value.store(stage_in);
    failed_stage_code.store(errorCode);
    printf("Stage '%s' failed: %d\n", LiGetStageName(stage_in), errorCode);
}

int N3dsConnectionListener::current_stage() { return stage.load(); }

int N3dsConnectionListener::failed_stage() { return failed_stage_value.load(); }

int N3dsConnectionListener::failed_stage_error() {
    return failed_stage_code.load();
}

void N3dsConnectionListener::arm_ui_handshake(int stage_in) {
    LightEvent_Clear(&teardown_request);
    LightEvent_Clear(&teardown_done);
    handshake_stage = stage_in;
    handshake_armed = true;
}

bool N3dsConnectionListener::ui_teardown_requested() {
    // Sticky event: stays signalled so it can be polled every frame.
    return LightEvent_TryWait(&teardown_request);
}

void N3dsConnectionListener::ui_teardown_ack() {
    LightEvent_Signal(&teardown_done);
}

N3dsConnectionListener::N3dsConnectionListener(bool enable_motion)
    : enable_motion(enable_motion) {
    LightEvent_Init(&teardown_request, RESET_STICKY);
    LightEvent_Init(&teardown_done, RESET_STICKY);
    MessageDispatcher::get_instance()->subscribe(
        MessageType::TOUCH_STATE_CHANGED, this);
    MessageDispatcher::get_instance()->subscribe(MessageType::EXIT_STREAM,
                                                 this);
}

N3dsConnectionListener::~N3dsConnectionListener() {
    MessageDispatcher::get_instance()->unsubscribe(
        MessageType::TOUCH_STATE_CHANGED, this);
    MessageDispatcher::get_instance()->unsubscribe(MessageType::EXIT_STREAM,
                                                   this);
}

void N3dsConnectionListener::accept(IMessage *msg) {
    switch (msg->getMessageType()) {
    case MessageType::TOUCH_STATE_CHANGED: {
        auto ttype = static_cast<TouchStateChangedMsg *>(msg)->ttype;
        debug.store(ttype == N3dsTouchType::DEBUG_TOUCH);
    } break;
    case MessageType::EXIT_STREAM: {
        connection_terminated(ML_ERROR_GRACEFUL_TERMINATION);
    } break;
    default:
        break;
    }
}

bool N3dsConnectionListener::is_connection_closed() {
    return connection_closed.load();
};

bool N3dsConnectionListener::has_terminated() { return terminated.load(); }

int N3dsConnectionListener::termination_code() { return term_code.load(); }

std::string N3dsConnectionListener::termination_message() {
    message_lock.lock();
    std::string copy = term_message;
    message_lock.unlock();
    return copy;
}

static void local_stage_starting(int stage) {
    auto instance = N3dsConnectionListener::get_instance();
    if (instance != nullptr) {
        instance->stage_starting(stage);
    }
}

static void local_stage_complete(int stage) {
    auto instance = N3dsConnectionListener::get_instance();
    if (instance != nullptr) {
        instance->stage_complete(stage);
    }
}

static void local_stage_failed(int stage, int errorCode) {
    auto instance = N3dsConnectionListener::get_instance();
    if (instance != nullptr) {
        instance->stage_failed(stage, errorCode);
    }
}

static void local_connection_terminated(int errorCode) {
    auto instance = N3dsConnectionListener::get_instance();
    if (instance != nullptr) {
        instance->connection_terminated(errorCode);
    }
}

static void local_connection_log_message(const char *format, ...) {
    auto instance = N3dsConnectionListener::get_instance();
    if (instance != nullptr) {
        va_list arglist;
        va_start(arglist, format);
        instance->connection_log_message(format, arglist);
        va_end(arglist);
    }
}

static void local_connection_status_update(int status) {
    auto instance = N3dsConnectionListener::get_instance();
    if (instance != nullptr) {
        instance->connection_status_update(status);
    }
}

static void local_set_motion_event_state(unsigned short controllerNumber,
                                         unsigned char motionType,
                                         unsigned short reportRateHz) {
    auto instance = N3dsConnectionListener::get_instance();
    if (instance != nullptr) {
        instance->set_motion_event_state(controllerNumber, motionType,
                                         reportRateHz);
    }
}

CONNECTION_LISTENER_CALLBACKS n3ds_connection_callbacks{
    .stageStarting = local_stage_starting,
    .stageComplete = local_stage_complete,
    .stageFailed = local_stage_failed,
    .connectionTerminated = local_connection_terminated,
    .logMessage = local_connection_log_message,
    .connectionStatusUpdate = local_connection_status_update,
    .setMotionEventState = local_set_motion_event_state,
};
