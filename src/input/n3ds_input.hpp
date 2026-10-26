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

#include "../config.hpp"
#include "../system/AtomicVar.hpp"
#include "../system/subscriber.hpp"
#include "touch/N3dsTouchscreenInput.hpp"
#include <stdbool.h>

class N3dsInput : public ISubscriber {
  public:
    // Takes the button mapping, mouse and motion options from cfg
    N3dsInput(int image_width, int image_height, const CONFIGURATION &cfg);
    ~N3dsInput();
    void accept(IMessage *msg) override;
    void n3dsinput_handle_event();
    void force_touchscreen_menu();

  private:
    void _add_gamepad();
    void _remove_gamepad();
    // Rebuilds buttons and triggers from the held keys via the mapping
    void _read_buttons(u32 held);
    bool _gamepad_state_changed();
    bool _accelerometer_state_changed();
    bool _gyroscope_state_changed();

  private:
    GAMEPAD_STATE gamepad_state, previous_state;
    std::unique_ptr<N3dsTouchscreenInput> touch_handler = nullptr;

    float gyro_coeff = 0;
    // Note: This was found experimentally and may need a calibration option in
    // settings
    float accel_coeff = 52.0;
    AtomicVar<bool> enable_gyro = false;
    AtomicVar<bool> enable_accel = false;
    bool use_triggers_for_mouse = false;
    bool motion_controls = false;
    bool menu_active = false;

    // Hold START to switch the C-stick between right stick and mouse
    bool cstick_mouse_enabled = true;
    bool cstick_mouse_active = false;
    u64 start_held_since = 0;   // system ticks, 0 = not held
    bool start_hold_handled = false;
    float mouse_remainder_x = 0, mouse_remainder_y = 0;

    // Hold L3 while the Circle Pad is pushed nearly all the way
    bool auto_sprint = false;
    bool sprint_active = false;
};
