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

#include "N3dsRenderer.hpp"

#include <cstdlib>
#include <cstring>
#include <stdbool.h>
#include <stdexcept>
#include <unistd.h>

// The two screens share (almost) the same physical pixel pitch but the
// bottom one is narrower: 320 px against the top screen's 400. To show the
// upper and lower halves of the stream at the same scale without squeezing
// the bottom half, the bottom screen shows the central 320/400 of the
// source width.
static int stretch_crop_width(int src_width) {
    int w = src_width * GSP_SCREEN_HEIGHT_BOTTOM / GSP_SCREEN_HEIGHT_TOP;
    // Keep the transfer source 8-pixel aligned
    return w & ~7;
}

static int stretch_crop_x(int src_width) {
    int x = (src_width - stretch_crop_width(src_width)) / 2;
    return x & ~7;
}

N3dsRendererDualScreenStretch::N3dsRendererDualScreenStretch(
    int dest_width, int dest_height, int src_width, int src_height, int px_size)
    : top_renderer(dest_width, dest_height, src_width, src_height / 2, px_size),
      bottom_renderer(stretch_crop_width(src_width), src_height / 2, px_size) {

    int px_offset_y = src_height / 2;
    int line_stride = MOON_CTR_VIDEO_TEX_W * px_size;
    source_offset =
        px_offset_y * line_stride + stretch_crop_x(src_width) * px_size;
}

N3dsRendererDualScreenStretch::~N3dsRendererDualScreenStretch() = default;

void N3dsRendererDualScreenStretch::write_px_to_framebuffer(uint8_t *source) {
    top_renderer.write_px_to_framebuffer(source);
    bottom_renderer.write_px_to_framebuffer(source + source_offset);
}

void N3dsRendererDualScreenStretch::set_perf_decode_ticks(u64 ticks) {
    top_renderer.set_perf_decode_ticks(ticks);
}
