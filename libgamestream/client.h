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

  /* Apollo extensions (ClassicOldSong/Apollo). Only Apollo sends these, so
   * isApollo is true whenever the Permission element was present. */
  bool isApollo;
  /* Bit set of APOLLO_PERM_* flags this client holds; 0 until paired. */
  unsigned int permission;
  /* Host-defined server commands, in the order the host sent them; the index
   * in this array is what LiSendExecServerCmd() takes. */
  char** serverCmds;
  int serverCmdCount;
  /* 1 = virtual display driver ready, 0 = not ready, -1 = not reported */
  int virtualDisplayDriverReady;
  char* currentGameUuid; /* optional */
} SERVER_DATA, *PSERVER_DATA;

/* Apollo client permission bits (crypto::PERM in Apollo's source) */
#define APOLLO_PERM_INPUT_CONTROLLER (1u << 8)
#define APOLLO_PERM_INPUT_TOUCH      (1u << 9)
#define APOLLO_PERM_INPUT_PEN        (1u << 10)
#define APOLLO_PERM_INPUT_MOUSE      (1u << 11)
#define APOLLO_PERM_INPUT_KEYBOARD   (1u << 12)
#define APOLLO_PERM_ALL_INPUTS       (0x1Fu << 8)
#define APOLLO_PERM_CLIPBOARD_SET    (1u << 16)
#define APOLLO_PERM_CLIPBOARD_READ   (1u << 17)
#define APOLLO_PERM_SERVER_CMD       (1u << 20)
#define APOLLO_PERM_LIST             (1u << 24)
#define APOLLO_PERM_VIEW             (1u << 25)
#define APOLLO_PERM_LAUNCH           (1u << 26)

/* App UUID Apollo gives its "Remote Input" (input only, no video) pseudo-app */
#define APOLLO_REMOTE_INPUT_UUID "8CB5C136-DA67-4F99-B4A1-F9CD35005CF4"
/* App ID of the single "Permission Denied" entry Apollo lists when the client
 * may not list apps */
#define APOLLO_PERMISSION_DENIED_APP_ID 114514

int gs_init(PSERVER_DATA server, char* address, unsigned short httpPort, const char *keyDirectory, int logLevel, bool unsupported);
void gs_cleanup();
/* appUuid is optional (Apollo); pass NULL or "" on other hosts. */
int gs_start_app(PSERVER_DATA server, PSTREAM_CONFIGURATION config, int appId, const char* appUuid, bool sops, bool localaudio, int gamepad_mask);
int gs_applist(PSERVER_DATA server, PAPP_LIST *app_list);
int gs_unpair(PSERVER_DATA server);
/* otpPassphrase: NULL for normal pairing (the PIN is typed on the host). With
 * Apollo's one-time PIN the PIN and passphrase come from the host's web UI
 * and are passed here instead. */
int gs_pair(PSERVER_DATA server, char* pin, const char* otpPassphrase);
/* Name this client reports to the host while pairing (default "New 3DS") */
void gs_set_device_name(const char* name);
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
