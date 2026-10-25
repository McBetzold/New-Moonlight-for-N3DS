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

#include "xml.h"

#include <Limelight.h>

#include <stdbool.h>

#define MIN_SUPPORTED_GFE_VERSION 3
#define MAX_SUPPORTED_GFE_VERSION 7

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _SERVER_DATA {
  char* gpuType;
  bool paired;
  bool unsupported;
  bool isNvidiaSoftware;
  int currentGame;
  int serverMajorVersion;
  char* gsVersion;
  PDISPLAY_MODE modes;
  SERVER_INFORMATION serverInfo;
  unsigned short httpPort;
  unsigned short httpsPort;
  char* hostname; /* optional, NULL when the server does not report one */
} SERVER_DATA, *PSERVER_DATA;

int gs_init(PSERVER_DATA server, char* address, unsigned short httpPort, const char *keyDirectory, int logLevel, bool unsupported);
void gs_cleanup();
int gs_start_app(PSERVER_DATA server, PSTREAM_CONFIGURATION config, int appId, bool sops, bool localaudio, int gamepad_mask);
int gs_applist(PSERVER_DATA server, PAPP_LIST *app_list);
int gs_unpair(PSERVER_DATA server);
int gs_pair(PSERVER_DATA server, char* pin);
int gs_quit_app(PSERVER_DATA server);
/* Downloads the box art image for an app. On success *out_data is a malloc'd
 * buffer of *out_size bytes that the caller must free(). */
int gs_download_boxart(PSERVER_DATA server, int appId, char **out_data, size_t *out_size);
/* Display mode to request from the host in place of the stream size (Apollo
 * sizes its virtual display from it and scales the picture down to the
 * stream). Pass 0 for either value to request the stream size again. */
void gs_set_host_display_mode(int width, int height);

#ifdef __cplusplus
}
#endif
