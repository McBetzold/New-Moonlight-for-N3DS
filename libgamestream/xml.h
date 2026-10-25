/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
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

#include <stdio.h>

typedef struct _APP_LIST {
  char* name;
  int id;
  /* Apollo only; NULL / -1 on other hosts. The UUID identifies the app across
   * list changes and idx is the host's own ordering of the list. */
  char* uuid;
  int idx;
  struct _APP_LIST *next;
} APP_LIST, *PAPP_LIST;

typedef struct _DISPLAY_MODE {
  unsigned int height;
  unsigned int width;
  unsigned int refresh;
  struct _DISPLAY_MODE *next;
} DISPLAY_MODE, *PDISPLAY_MODE;

int xml_search(char* data, size_t len, char* node, char** result);
/* Collects the text of every element named `node`. On success *results is a
 * malloc'd array of *count malloc'd strings (both to be freed by the caller);
 * *count is 0 and *results NULL when there are none. */
int xml_search_all(char* data, size_t len, const char* node, char*** results, int* count);
int xml_applist(char* data, size_t len, PAPP_LIST *app_list);
int xml_modelist(char* data, size_t len, PDISPLAY_MODE *mode_list);
int xml_status(char* data, size_t len);
