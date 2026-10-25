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

#pragma once

#include "../system/AtomicVar.hpp"
#include "../system/ThreadLock.hpp"
#include "../system/subscriber.hpp"
#include <3ds.h>
#include <Limelight.h>
#include <memory>
#include <string>

class N3dsConnectionListener : public ISubscriber {
  public:
    N3dsConnectionListener(bool enable_motion);
    ~N3dsConnectionListener();

    void accept(IMessage *msg) override;

    static N3dsConnectionListener *create_instance(bool enable_motion) {
        if (instance == nullptr) {
            instance = std::make_unique<N3dsConnectionListener>(enable_motion);
        }
        return instance.get();
    }
    static N3dsConnectionListener *get_instance() {
        return instance != nullptr ? instance.get() : nullptr;
    }
    static void destroy_instance() { instance = nullptr; }

    void connection_terminated(int errorCode);
    void connection_log_message(const char *format, va_list arglist);
    void connection_status_update(int status);
    void set_motion_event_state(unsigned short controllerNumber,
                                unsigned char motionType,
                                unsigned short reportRateHz);

    // Stage progress, reported on the thread that runs LiStartConnection.
    void stage_starting(int stage);
    void stage_complete(int stage);
    void stage_failed(int stage, int errorCode);
    int current_stage();
    int failed_stage();
    int failed_stage_error();

    // The launcher UI owns the GPU until the video decoder is created inside
    // LiStartConnection. With the handshake armed, stage_starting(stage)
    // raises a request and blocks until the UI thread has released the GPU
    // and called ui_teardown_ack().
    void arm_ui_handshake(int stage);
    bool ui_teardown_requested();
    void ui_teardown_ack();

    bool is_connection_closed();
    bool has_terminated();
    int termination_code();
    std::string termination_message();
    // moonlight-common-c's verdict on recent packet loss (CONN_STATUS_POOR)
    bool connection_is_poor();

  private:
    static std::unique_ptr<N3dsConnectionListener> instance;
    bool enable_motion;
    AtomicVar<bool> debug = false;
    AtomicVar<bool> connection_closed = false;
    AtomicVar<bool> connection_poor = false;

    AtomicVar<int> stage = STAGE_NONE;
    AtomicVar<int> failed_stage_value = STAGE_NONE;
    AtomicVar<int> failed_stage_code = 0;

    bool handshake_armed = false;
    int handshake_stage = STAGE_NONE;
    LightEvent teardown_request;
    LightEvent teardown_done;

    AtomicVar<bool> terminated = false;
    AtomicVar<int> term_code = 0;
    ThreadLock message_lock;
    std::string term_message;
};

extern CONNECTION_LISTENER_CALLBACKS n3ds_connection_callbacks;
