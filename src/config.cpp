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

#include "config.hpp"
#include "audio/audio.h"
#include "system/pair_record.hpp"
#include "util.h"

#include <cstring>
#include <fstream>
#include <getopt.h>

#define MOONLIGHT_PATH "/moonlight"
#define USER_PATHS "."
#define DEFAULT_CONFIG_DIR "/.config"
#define DEFAULT_CACHE_DIR "/.cache"

#define write_config_string(fd, key, value) fprintf(fd, "%s = %s\n", key, value)
#define write_config_int(fd, key, value) fprintf(fd, "%s = %d\n", key, value)
#define write_config_bool(fd, key, value)                                      \
    fprintf(fd, "%s = %s\n", key, value ? "true" : "false")

static struct option long_options[] = {
    {"width", required_argument, NULL, 'c'},
    {"height", required_argument, NULL, 'd'},
    {"bitrate", required_argument, NULL, 'g'},
    {"packetsize", required_argument, NULL, 'h'},
    {"app", required_argument, NULL, 'i'},
    {"sops", required_argument, NULL, 'l'},
    {"localaudio", required_argument, NULL, 'n'},
    {"fps", required_argument, NULL, 'v'},
    {"quitappafter", required_argument, NULL, '1'},
    {"viewonly", required_argument, NULL, '2'},
    {"port", required_argument, NULL, '6'},
    {"video_decoder", required_argument, NULL, '8'},
    {"motion_controls", required_argument, NULL, 'e'},
    {"swapfacebuttons", required_argument, NULL, 'A'},
    {"swaptriggersandshoulders", required_argument, NULL, 'B'},
    {"usetriggersformouse", required_argument, NULL, 'C'},
    {"host_width", required_argument, NULL, 'D'},
    {"host_height", required_argument, NULL, 'E'},
    {"stereo_3d", required_argument, NULL, 'F'},
    {"swap_eyes", required_argument, NULL, 'G'},
    {"volume_boost", required_argument, NULL, 'H'},
    {"auto_reconnect", required_argument, NULL, 'I'},
    {"resume_on_start", required_argument, NULL, 'J'},
    {"cstick_mouse", required_argument, NULL, 'K'},
    {0, 0, 0, 0},
};

// Names used in moonlight.conf, indexed by STEREO_3D_MODE
static const char *stereo_3d_names[STEREO_3D_MODE_COUNT] = {"off", "sbs",
                                                            "depth"};

const char *stereo_3d_mode_name(STEREO_3D_MODE mode) {
    int i = (int)mode;
    if (i < 0 || i >= STEREO_3D_MODE_COUNT) {
        i = STEREO_3D_OFF;
    }
    return stereo_3d_names[i];
}

static STEREO_3D_MODE parse_stereo_3d(const char *value) {
    if (value == NULL) {
        return STEREO_3D_OFF;
    }
    for (int i = 0; i < STEREO_3D_MODE_COUNT; i++) {
        if (strcmp(value, stereo_3d_names[i]) == 0) {
            return (STEREO_3D_MODE)i;
        }
    }
    if (strcmp(value, "side_by_side") == 0) {
        return STEREO_3D_SIDE_BY_SIDE;
    }
    int n = atoi(value);
    if (n > 0 && n < STEREO_3D_MODE_COUNT) {
        return (STEREO_3D_MODE)n;
    }
    return STEREO_3D_OFF;
}

void parse_argument(int c, char *value, PCONFIGURATION config) {
    switch (c) {
    case 'c':
        config->stream.width = atoi(value);
        break;
    case 'd':
        config->stream.height = atoi(value);
        break;
    case 'e':
        config->motion_controls =
            ((value != NULL) && (strcmp(value, "true") == 0));
        break;
    case 'g':
        config->stream.bitrate = atoi(value);
        break;
    case 'h':
        config->stream.packetSize = atoi(value);
        break;
    case 'i':
        config->app = value;
        break;
    case 'l':
        config->sops = ((value == NULL) || (strcmp(value, "false") != 0));
        break;
    case 'n':
        config->localaudio = ((value != NULL) && (strcmp(value, "true") == 0));
        break;
    case 'v':
        config->stream.fps = atoi(value);
        break;
    case '1':
        config->quitappafter =
            ((value != NULL) && (strcmp(value, "true") == 0));
        break;
    case '2':
        config->viewonly = ((value != NULL) && (strcmp(value, "true") == 0));
        break;
    case '6':
        config->port = atoi(value);
        break;
    case '8':
        if (value == NULL) {
            config->video_decoder = VIDEO_DECODER_TYPE::HARDWARE_VIDEO_DECODER;
        } else {
            config->video_decoder = (VIDEO_DECODER_TYPE)atoi(value);
        }
        break;
    case 'A':
        config->swap_face_buttons =
            ((value != NULL) && (strcmp(value, "true") == 0));
        break;
    case 'B':
        config->swap_triggers_and_shoulders =
            ((value != NULL) && (strcmp(value, "true") == 0));
        break;
    case 'C':
        config->use_triggers_for_mouse =
            ((value != NULL) && (strcmp(value, "true") == 0));
        break;
    case 'D':
        config->host_width = value != NULL ? atoi(value) : 0;
        break;
    case 'E':
        config->host_height = value != NULL ? atoi(value) : 0;
        break;
    case 'F':
        config->stereo_3d = parse_stereo_3d(value);
        break;
    case 'G':
        config->swap_eyes = ((value != NULL) && (strcmp(value, "true") == 0));
        break;
    case 'H': {
        int v = value != NULL ? atoi(value) : 100;
        config->volume_boost = v < 100 ? 100 : v > 400 ? 400 : v;
        break;
    }
    case 'I':
        config->auto_reconnect =
            ((value != NULL) && (strcmp(value, "true") == 0));
        break;
    case 'J':
        config->resume_on_start =
            ((value != NULL) && (strcmp(value, "true") == 0));
        break;
    case 'K':
        config->cstick_mouse = ((value != NULL) && (strcmp(value, "true") == 0));
        break;
    case 1:
        if (config->action == NULL)
            config->action = value;
        else if (config->address == NULL)
            config->address = value;
        else {
            perror("Too many options");
            exit(-1);
        }
        break;
    }
}

bool config_file_parse(PCONFIGURATION config) {
    std::ifstream config_file(MOONLIGHT_3DS_PATH "/moonlight.conf");
    std::string line;
    while (std::getline(config_file, line)) {
        char *key = NULL, *value = NULL;
        if (sscanf(line.c_str(), "%ms = %m[^\n]", &key, &value) == 2) {
            if (strcmp(key, "address") == 0) {
                config->address = value;
            } else {
                for (int i = 0; long_options[i].name != NULL; i++) {
                    if (strcmp(long_options[i].name, key) == 0) {
                        if (long_options[i].has_arg == required_argument)
                            parse_argument(long_options[i].val, value, config);
                        else if (strcmp("true", value) == 0)
                            parse_argument(long_options[i].val, NULL, config);
                    }
                }
            }
        }
    }
    return true;
}

void config_save(char *filename, PCONFIGURATION config) {
    FILE *fd = fopen(filename, "w");
    if (fd == NULL) {
        fprintf(stderr, "Can't open configuration file: %s\n", filename);
        exit(EXIT_FAILURE);
    }

    write_config_int(fd, "width", config->stream.width);
    write_config_int(fd, "height", config->stream.height);
    write_config_int(fd, "host_width", config->host_width);
    write_config_int(fd, "host_height", config->host_height);
    write_config_int(fd, "fps", config->stream.fps);
    write_config_int(fd, "bitrate", config->stream.bitrate);
    write_config_int(fd, "packetsize", config->stream.packetSize);
    write_config_bool(fd, "sops", config->sops);
    write_config_bool(fd, "localaudio", config->localaudio);
    write_config_bool(fd, "quitappafter", config->quitappafter);
    write_config_bool(fd, "viewonly", config->viewonly);
    write_config_int(fd, "video_decoder", config->video_decoder);
    write_config_bool(fd, "swapfacebuttons", config->swap_face_buttons);
    write_config_bool(fd, "swaptriggersandshoulders",
                      config->swap_triggers_and_shoulders);
    write_config_bool(fd, "usetriggersformouse",
                      config->use_triggers_for_mouse);
    write_config_bool(fd, "motion_controls", config->motion_controls);
    write_config_string(fd, "stereo_3d",
                        stereo_3d_mode_name(config->stereo_3d));
    write_config_bool(fd, "swap_eyes", config->swap_eyes);
    write_config_int(fd, "volume_boost", config->volume_boost);
    write_config_bool(fd, "auto_reconnect", config->auto_reconnect);
    write_config_bool(fd, "resume_on_start", config->resume_on_start);
    write_config_bool(fd, "cstick_mouse", config->cstick_mouse);

    if (strcmp(config->app, "Steam") != 0)
        write_config_string(fd, "app", config->app);

    fclose(fd);
}

void config_parse(int argc, char *argv[], PCONFIGURATION config) {
    LiInitializeStreamConfiguration(&config->stream);

    config->stream.bitrate = -1;
    config->stream.packetSize = 1392;
    config->stream.streamingRemotely = STREAM_CFG_AUTO;
    config->stream.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    config->stream.supportedVideoFormats = SCM_H264;

    config->platform = "auto";
    config->app = "Steam";
    config->action = NULL;
    config->address = NULL;
    config->config_file = NULL;
    config->audio_device = NULL;
    config->sops = true;
    config->localaudio = false;
    config->unsupported = true;
    config->quitappafter = false;
    config->viewonly = false;
    config->port = 47989;

    strcpy(config->key_dir, MOONLIGHT_3DS_PATH "/keys");

    config->stream.width = 800;
    config->stream.height = 480;
    config->host_width = 0;
    config->host_height = 0;
    config->stream.fps = 60;
    config->stream.encryptionFlags = ENCFLG_NONE;
    config->video_decoder = VIDEO_DECODER_TYPE::HARDWARE_VIDEO_DECODER;
    config->motion_controls = false;
    config->swap_face_buttons = false;
    config->swap_triggers_and_shoulders = false;
    config->use_triggers_for_mouse = false;
    config->stereo_3d = STEREO_3D_OFF;
    config->swap_eyes = false;
    config->volume_boost = 100;
    config->auto_reconnect = true;
    config->resume_on_start = false;
    config->cstick_mouse = true;

    config_file_parse(config);

    if (config->stream.bitrate == -1) {
        // This table prefers 16:10 resolutions because they are
        // only slightly more pixels than the 16:9 equivalents, so
        // we don't want to bump those 16:10 resolutions up to the
        // next 16:9 slot.

        if (config->stream.width * config->stream.height <= 640 * 360) {
            config->stream.bitrate = (int)(1000 * (config->stream.fps / 30.0));
        } else if (config->stream.width * config->stream.height <= 854 * 480) {
            config->stream.bitrate = (int)(1500 * (config->stream.fps / 30.0));
        } else if (config->stream.width * config->stream.height <= 1366 * 768) {
            // This covers 1280x720 and 1280x800 too
            config->stream.bitrate = (int)(5000 * (config->stream.fps / 30.0));
        } else if (config->stream.width * config->stream.height <=
                   1920 * 1200) {
            config->stream.bitrate = (int)(10000 * (config->stream.fps / 30.0));
        } else if (config->stream.width * config->stream.height <=
                   2560 * 1600) {
            config->stream.bitrate = (int)(20000 * (config->stream.fps / 30.0));
        } else /* if (config->stream.width * config->stream.height <= 3840 *
                  2160) */
        {
            config->stream.bitrate = (int)(40000 * (config->stream.fps / 30.0));
        }
    }
}
