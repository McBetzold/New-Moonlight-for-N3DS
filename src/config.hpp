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

#include <Limelight.h>

#include <stdbool.h>

#define MAX_INPUTS 6

enum VIDEO_DECODER_TYPE {
    HARDWARE_VIDEO_DECODER = 0,
    SOFTWARE_VIDEO_DECODER,
    DISABLE_VIDEO_DECODER
};

// What the top screen does when the 3D slider is up.
enum STEREO_3D_MODE {
    // The slider is ignored and the stream always shows in 2D.
    STEREO_3D_OFF = 0,
    // The host sends half side-by-side 3D: the left half of the picture goes
    // to the left eye and the right half to the right eye.
    STEREO_3D_SIDE_BY_SIDE,
    // Any 2D stream: both eyes see the whole picture, offset so it sits
    // behind the screen like a window. The slider sets the depth.
    STEREO_3D_FLAT_DEPTH,
    STEREO_3D_MODE_COUNT
};

// What a 3DS button (or a zone of the bottom-screen gamepad view) sends to
// the PC's virtual controller. See src/input/button_mapping.hpp.
enum PAD_TARGET {
    PAD_NONE = 0,
    PAD_A,
    PAD_B,
    PAD_X,
    PAD_Y,
    PAD_LB,
    PAD_RB,
    PAD_LT,
    PAD_RT,
    PAD_VIEW,  // Back / Select
    PAD_MENU,  // Start
    PAD_GUIDE, // Xbox / Home button
    PAD_L3,
    PAD_R3,
    PAD_L3R3, // both stick clicks at once
    PAD_UP,
    PAD_DOWN,
    PAD_LEFT,
    PAD_RIGHT,
    PAD_TARGET_COUNT
};

// The 3DS buttons that can be remapped (the D-pad and sticks are fixed)
enum PAD_SOURCE {
    SRC_A = 0,
    SRC_B,
    SRC_X,
    SRC_Y,
    SRC_L,
    SRC_R,
    SRC_ZL,
    SRC_ZR,
    SRC_START,
    SRC_SELECT,
    PAD_SOURCE_COUNT
};

// Touch zones of the bottom-screen gamepad view
enum TOUCH_ZONE {
    ZONE_TOP_LEFT = 0,
    ZONE_TOP_MIDDLE,
    ZONE_TOP_RIGHT,
    ZONE_BOTTOM,
    TOUCH_ZONE_COUNT
};

typedef struct _CONFIGURATION {
    STREAM_CONFIGURATION stream;
    char *app;
    char *action;
    char *address;
    char *mapping;
    char *platform;
    char *audio_device;
    char *config_file;
    char key_dir[4096];
    bool sops;
    bool localaudio;
    bool unsupported;
    bool quitappafter;
    bool viewonly;
    bool mouse_emulation;
    char *inputs[MAX_INPUTS];
    int inputsCount;
    bool hdr;
    unsigned short port;
    VIDEO_DECODER_TYPE video_decoder;
    bool motion_controls;
    bool swap_face_buttons;
    bool swap_triggers_and_shoulders;
    bool use_triggers_for_mouse;
    // Display mode asked of the host instead of the stream size; 0 = same as
    // the stream. Apollo creates its virtual display at this size and scales
    // the picture down, so games see a normal monitor.
    int host_width;
    int host_height;
    STEREO_3D_MODE stereo_3d;
    // Exchange the left- and right-eye pictures in either 3D mode.
    bool swap_eyes;
    // Audio gain in percent (100 = as sent). The 3DS speakers are quiet.
    int volume_boost;
    // Reconnect by itself when a stream drops unexpectedly.
    bool auto_reconnect;
    // On start-up, resume the app still running on a paired PC.
    bool resume_on_start;
    // Holding START turns the C-stick into a mouse (ZL/ZR click).
    bool cstick_mouse;
    // Hold L3 while the Circle Pad is pushed almost fully (sprint in FPS games)
    bool auto_sprint;
    // PAD_TARGET for each PAD_SOURCE and each TOUCH_ZONE
    unsigned char button_map[PAD_SOURCE_COUNT];
    unsigned char touch_map[TOUCH_ZONE_COUNT];
    // Set while parsing when moonlight.conf has a button_map line; otherwise
    // the old swap options are converted into a mapping
    bool button_map_from_file;
} CONFIGURATION, *PCONFIGURATION;

const char *stereo_3d_mode_name(STEREO_3D_MODE mode);

bool config_file_parse(char *filename, PCONFIGURATION config);
void config_parse(int argc, char *argv[], PCONFIGURATION config);
void parse_argument(int c, char *value, PCONFIGURATION config);
// Returns false if the file could not be written
bool config_save(char *filename, PCONFIGURATION config);
