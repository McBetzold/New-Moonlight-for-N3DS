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
#include "../../system/debug_console.hpp"
#include "../../system/apt_state.hpp"
#include "../../system/n3ds_connection.hpp"
#include "vshader_shbin.h"

#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdbool.h>
#include <stdexcept>
#include <algorithm>
#include <unistd.h>
#include <utility>

STEREO_3D_MODE N3dsRendererBase::stereo_mode = STEREO_3D_OFF;
bool N3dsRendererBase::stereo_swap_eyes = false;

volatile int N3dsRendererBase::last_frame_number = -1;
volatile u32 N3dsRendererBase::frames_lost = 0;
volatile u32 N3dsRendererBase::host_latency_sum = 0;
volatile u32 N3dsRendererBase::host_latency_max = 0;
volatile u32 N3dsRendererBase::host_latency_count = 0;

void N3dsRendererBase::set_stereo_options(STEREO_3D_MODE mode,
                                          bool swap_eyes) {
    stereo_mode = mode;
    stereo_swap_eyes = swap_eyes;
}

volatile u64 N3dsRendererBase::last_frame_tick = 0;
volatile u64 N3dsRendererBase::last_trouble_tick = 0;
volatile bool N3dsRendererBase::mouse_indicator = false;

void N3dsRendererBase::set_mouse_indicator(bool on) { mouse_indicator = on; }

void N3dsRendererBase::note_frame(int frame_number,
                                  unsigned host_latency_01ms) {
    u64 now = svcGetSystemTick();
    // A long silence means this is the first frame of a new stream; start the
    // bookkeeping over instead of reporting the gap as a stall
    if (last_frame_tick != 0 && now - last_frame_tick > 5 * SYSCLOCK_ARM11) {
        last_frame_tick = 0;
        last_frame_number = -1;
    }
    // moonlight-common-c numbers frames consecutively and drops the ones it
    // could not repair with FEC, so a gap is a frame lost on the network.
    if (last_frame_number >= 0 && frame_number > last_frame_number + 1) {
        frames_lost += (u32)(frame_number - last_frame_number - 1);
        last_trouble_tick = now;
    }
    // Weak Wi-Fi often delays frames (retransmissions) rather than losing
    // them; a quarter-second gap is a stall worth flagging too. The host
    // sends a frame at least every ~100 ms even when nothing changes.
    if (last_frame_tick != 0 && now - last_frame_tick > SYSCLOCK_ARM11 / 4) {
        last_trouble_tick = now;
    }
    last_frame_tick = now;
    last_frame_number = frame_number;
    if (host_latency_01ms > 0) {
        host_latency_sum += host_latency_01ms;
        host_latency_count++;
        if (host_latency_01ms > host_latency_max) {
            host_latency_max = host_latency_01ms;
        }
    }
}

// Draws a small bitmap into the decoded picture, before the GPU scales it to
// the screen, so it travels the same path as the video. (sx, sy) is in
// 400x240 top-screen coordinates; each icon pixel becomes a block of picture
// pixels so the icon has the same on-screen size at any stream resolution.
// The picture is row-major RGB565 with a row stride of MOON_CTR_VIDEO_TEX_W.
// In each row '#' is drawn in `hash`, '.' in `dot`, anything else is skipped.
static void blit_picture(uint8_t *source, int image_w, int image_h,
                         const char *const *rows, int row_count, int sx,
                         int sy, u16 hash, u16 dot) {
    u16 *pic = (u16 *)source;
    int bw = std::max(1, image_w / GSP_SCREEN_HEIGHT_TOP);
    int bh = std::max(1, image_h / GSP_SCREEN_WIDTH);
    int x0 = sx * image_w / GSP_SCREEN_HEIGHT_TOP;
    int y0 = sy * image_h / GSP_SCREEN_WIDTH;
    for (int r = 0; r < row_count; r++) {
        for (int c = 0; rows[r][c]; c++) {
            char ch = rows[r][c];
            if (ch != '#' && ch != '.') {
                continue;
            }
            u16 color = ch == '#' ? hash : dot;
            for (int dy = 0; dy < bh; dy++) {
                int y = y0 + r * bh + dy;
                if (y < 0 || y >= image_h) {
                    continue;
                }
                for (int dx = 0; dx < bw; dx++) {
                    int x = x0 + c * bw + dx;
                    if (x >= 0 && x < image_w) {
                        pic[y * MOON_CTR_VIDEO_TEX_W + x] = color;
                    }
                }
            }
        }
    }
    // The GPU reads the picture from memory; push the CPU's writes out of
    // its cache first
    int first_row = std::max(0, y0);
    int last_row = std::min(image_h, y0 + row_count * bh);
    if (last_row > first_row) {
        GSPGPU_FlushDataCache(pic + first_row * MOON_CTR_VIDEO_TEX_W,
                              (last_row - first_row) * MOON_CTR_VIDEO_TEX_W *
                                  sizeof(u16));
    }
}

static const u16 COLOR_YELLOW = 0xFFE0, COLOR_DARK = 0x2104,
                 COLOR_WHITE = 0xFFFF;

// Yellow warning sign with an exclamation mark
static const char *const WARNING_ICON[] = {
    "##############", "#.....##.....#", "#.....##.....#", "#.....##.....#",
    "#.....##.....#", "#.....##.....#", "#.....##.....#", "#............#",
    "#.....##.....#", "#.....##.....#", "#............#", "##############",
};

// Mouse pointer (dark outline, white fill)
static const char *const POINTER_ICON[] = {
    "#",          "##",         "#.#",       "#..#",       "#...#",
    "#....#",     "#.....#",    "#......#",  "#.......#",  "#........#",
    "#.....#####", "#..#..#",   "#.# #..#",  "##  #..#",   "#    #..#",
    "      ##",
};

void N3dsRendererBase::draw_connection_marker(uint8_t *source,
                                              bool side_by_side) {
    auto listener = N3dsConnectionListener::get_instance();
    bool poor = listener != nullptr && listener->connection_is_poor();
    // Frames lost or stalled within the last three seconds
    bool recent = last_trouble_tick != 0 &&
                  svcGetSystemTick() - last_trouble_tick < 3 * SYSCLOCK_ARM11;
    bool mouse = mouse_indicator;
    if (!poor && !recent && !mouse) {
        return;
    }
    // In side-by-side 3D each half of the picture is one eye; draw in both
    // halves at the matching spot so the icon sits on the screen plane
    int halves = side_by_side ? 2 : 1;
    int half_w = image_width / halves;
    for (int h = 0; h < halves; h++) {
        uint8_t *base = source + h * half_w * sizeof(u16);
        if (poor || recent) {
            blit_picture(base, half_w, image_height, WARNING_ICON,
                         sizeof(WARNING_ICON) / sizeof(WARNING_ICON[0]),
                         GSP_SCREEN_HEIGHT_TOP - 6 - 14, 5, COLOR_DARK,
                         COLOR_YELLOW);
        }
        if (mouse) {
            // The C-stick is driving the mouse (hold START to switch back)
            blit_picture(base, half_w, image_height, POINTER_ICON,
                         sizeof(POINTER_ICON) / sizeof(POINTER_ICON[0]), 6, 5,
                         COLOR_DARK, COLOR_WHITE);
        }
    }
}

N3dsRendererBase::N3dsRendererBase(gfxScreen_t screen_in, int surface_width_in,
                                   int surface_height_in, int image_width_in,
                                   int image_height_in, int pixel_size,
                                   bool debug_in)
    : screen(screen_in), surface_width(surface_width_in),
      surface_height(surface_height_in), image_width(image_width_in),
      image_height(image_height_in), debug(debug_in), px_size(pixel_size) {
    cmdlist = (u32 *)linearAlloc(CMDLIST_SZ * 4);
    // With a 3D mode on, a 400-wide stream also needs room for two 400-column
    // eye pictures.
    fb_rows = surface_width;
    if (screen == GFX_TOP && stereo_mode != STEREO_3D_OFF &&
        fb_rows < GSP_SCREEN_HEIGHT_TOP_2X) {
        fb_rows = GSP_SCREEN_HEIGHT_TOP_2X;
    }
    vramFb = vramAlloc(fb_rows * surface_height * px_size);
    // Needs to be able to hold an 800x480
    vramTex = vramAlloc(MOON_CTR_VIDEO_TEX_W * MOON_CTR_VIDEO_TEX_H * px_size);
}

N3dsRendererBase::~N3dsRendererBase() {
    linearFree(cmdlist);
    vramFree(vramFb);
    vramFree(vramTex);

    // Clear target screen
    u8 *framebuf = gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL);
    memset(framebuf, 0, surface_width * surface_height * px_size);
    gfxScreenSwapBuffers(screen, true);

    // Return to the default display width before exiting
    if (surface_width == GSP_SCREEN_HEIGHT_TOP_2X) {
        gfxSetWide(false);
    }
}

void N3dsRendererBase::ensure_3d_enabled() {
    if (!gfxIs3D()) {
        gfxSetWide(false);
        gfxSet3D(true);
    }
}

void N3dsRendererBase::ensure_3d_disabled() {
    if (gfxIs3D()) {
        gfxSet3D(false);
    }
    if (surface_width == GSP_SCREEN_HEIGHT_TOP_2X) {
        gfxSetWide(true);
    }
}

inline void N3dsRendererBase::write24(u8 *p, u32 val) {
    p[0] = val;
    p[1] = val >> 8;
    p[2] = val >> 16;
}

// Refresh the debug view's status panel once per second with the presented
// frame rate, the per-frame timings and the estimated network round trip.
inline void N3dsRendererBase::log_perf_counters() {
    perf_log_frames++;
    perf_log_decode_sum += perf_decode_ticks;
    perf_log_fbcopy_sum += perf_fbcopy_ticks;
    if (perf_decode_ticks > perf_log_decode_max)
        perf_log_decode_max = perf_decode_ticks;
    if (perf_fbcopy_ticks > perf_log_fbcopy_max)
        perf_log_fbcopy_max = perf_fbcopy_ticks;

    u64 now = svcGetSystemTick();
    if (perf_log_last_ticks == 0) {
        perf_log_last_ticks = now;
        return;
    }
    if (now - perf_log_last_ticks < SYSCLOCK_ARM11) {
        return;
    }

    const double ms_per_tick = 1000.0 / (double)SYSCLOCK_ARM11;
    double elapsed_s =
        (double)(now - perf_log_last_ticks) / (double)SYSCLOCK_ARM11;
    uint32_t rtt_ms = 0, rtt_var_ms = 0;
    char rtt_str[24];
    if (LiGetEstimatedRttInfo(&rtt_ms, &rtt_var_ms)) {
        snprintf(rtt_str, sizeof(rtt_str), "%lu ms (var %lu)",
                 (unsigned long)rtt_ms, (unsigned long)rtt_var_ms);
    } else {
        snprintf(rtt_str, sizeof(rtt_str), "n/a");
    }
    // Host-side latency (capture to send) and network losses come from the
    // receive thread via note_frame()
    u32 lost = frames_lost;
    u32 hl_sum = host_latency_sum, hl_max = host_latency_max,
        hl_count = host_latency_count;
    frames_lost = 0;
    host_latency_sum = host_latency_max = host_latency_count = 0;
    char host_str[40];
    if (hl_count > 0) {
        snprintf(host_str, sizeof(host_str), "avg %5.1f  max %5.1f ms",
                 hl_sum / (double)hl_count / 10.0, hl_max / 10.0);
    } else {
        // The host only times frames it newly captured; on a still screen
        // it re-sends the last frame without a time
        snprintf(host_str, sizeof(host_str), "n/a (screen not changing)");
    }

    // The RTT is moonlight-common-c's estimate from the ENet control channel,
    // whose acks are serviced on a coarse timer on the 3DS; it reads far
    // higher than an ICMP ping and is only useful for spotting changes.
    // "lost" counts frames the network dropped beyond FEC repair; the 3DS
    // never drops a frame it received.
    DebugConsole::set_stats(
        "%.0f fps  %dx%d  lost %lu  rtt %s\n"
        "decode  avg %5.1f  max %5.1f ms\n"
        "present avg %5.1f  max %5.1f ms\n"
        "host    %s",
        perf_log_frames / elapsed_s, image_width, image_height,
        (unsigned long)lost, rtt_str,
        (perf_log_decode_sum / (double)perf_log_frames) * ms_per_tick,
        perf_log_decode_max * ms_per_tick,
        (perf_log_fbcopy_sum / (double)perf_log_frames) * ms_per_tick,
        perf_log_fbcopy_max * ms_per_tick, host_str);

    perf_log_last_ticks = now;
    perf_log_frames = 0;
    perf_log_decode_sum = perf_log_decode_max = 0;
    perf_log_fbcopy_sum = perf_log_fbcopy_max = 0;
}

void N3dsRendererBase::write_px_to_framebuffer_gpu(
    uint8_t *__restrict source, const N3dsStereoView &stereo) {
    // Do nothing when GPU right is lost, otherwise we hang when going to
    // the home menu. The APT hook fires before the right is actually taken
    // away, which closes the window where a transfer could be started
    // mid-transition.
    if (apt_state_suspended() || !gspHasGpuRight()) {
        return;
    }

    u64 start_ticks = svcGetSystemTick();

    // A 3D frame is drawn as the left-eye picture in the first 400 rows of
    // vramFb and the right-eye picture in the next 400.
    const bool stereo_on = stereo.enabled && screen == GFX_TOP &&
                           fb_rows >= GSP_SCREEN_HEIGHT_TOP_2X;
    const int rt_rows = stereo_on ? GSP_SCREEN_HEIGHT_TOP_2X : surface_width;

    // NOTE: At 800x480, we can display the _width_ natively, but the height
    // needs to be downsampled. MVD is incapable of downsampling, so we have to
    // do it on the GPU.

    // TODO: If we can use rotation from the decoder, we can do a 2x downscale
    // using display transfer and skip P3D. Not necessary because PICA is
    // significantly faster than the decoder.

    // Status icons (poor connection, C-stick mouse) are painted into the
    // picture itself so they reach the screen with the video
    if (screen == GFX_TOP) {
        draw_connection_marker(source, stereo_on && !stereo.full_frame);
    }

    // Tile the source image into the scratch buffer.
    GX_DisplayTransfer(
        (u32 *)source,
        GX_BUFFER_DIM(MOON_CTR_VIDEO_TEX_W, MOON_CTR_VIDEO_TEX_H),
        (u32 *)vramTex,
        GX_BUFFER_DIM(MOON_CTR_VIDEO_TEX_W, MOON_CTR_VIDEO_TEX_H),
        GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) |
            GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
            GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565));

    // While the transfer is running, create a temporary command list to rotate
    // the framebuffer into source
    GPUCMD_SetBuffer(cmdlist, CMDLIST_SZ, 0);

    // TODO: Verify this mitigates rounding errors due to f24 precision issues.

#define C GPUCMD_AddWrite

    C(GPUREG_FRAMEBUFFER_INVALIDATE, 1);
    C(GPUREG_COLORBUFFER_LOC, osConvertVirtToPhys(vramFb) >> 3);
    C(GPUREG_DEPTHBUFFER_LOC, 0);
    C(GPUREG_RENDERBUF_DIM, (1 << 24) | ((rt_rows - 1) << 12) | surface_height);
    C(GPUREG_FRAMEBUFFER_DIM,
      (1 << 24) | ((rt_rows - 1) << 12) | surface_height);
    C(GPUREG_FRAMEBUFFER_BLOCK32, 0);

    C(GPUREG_DEPTH_COLOR_MASK, 0xF << 8); // Write RGBA, no depth
    C(GPUREG_EARLYDEPTH_TEST1, 0);
    C(GPUREG_EARLYDEPTH_TEST2, 0);
    C(GPUREG_COLORBUFFER_FORMAT, GPU_RGB565 << 16);
    C(GPUREG_COLORBUFFER_READ,
      0x0); // Buffer is uninitialized and should not be read.
    C(GPUREG_COLORBUFFER_WRITE, 0xF);
    C(GPUREG_DEPTHBUFFER_READ, 0); // No depth buffer
    C(GPUREG_DEPTHBUFFER_WRITE, 0);

    C(GPUREG_VIEWPORT_XY, 0);

    C(GPUREG_VIEWPORT_WIDTH, f32tof24(surface_height / 2));
    C(GPUREG_VIEWPORT_INVW, f32tof31(2.0 / ((double)surface_height)) << 1);
    C(GPUREG_VIEWPORT_HEIGHT, f32tof24(rt_rows / 2));
    C(GPUREG_VIEWPORT_INVH, f32tof31(2.0 / ((double)rt_rows)) << 1);

    C(GPUREG_SCISSORTEST_MODE, 0);
    C(GPUREG_SCISSORTEST_POS, 0);
    C(GPUREG_SCISSORTEST_DIM, 0);

    C(GPUREG_DEPTHMAP_ENABLE, 1);
    C(GPUREG_DEPTHMAP_SCALE, f32tof24(-1.0));
    C(GPUREG_DEPTHMAP_OFFSET, 0);
    C(GPUREG_STENCIL_TEST, 0);
    C(GPUREG_FRAGOP_ALPHA_TEST, 0);
    C(GPUREG_LOGIC_OP, 3);
    C(GPUREG_COLOR_OPERATION, 0x00E40000);

    // Texturing
    C(GPUREG_TEXUNIT0_TYPE, GPU_RGB565);
    C(GPUREG_TEXUNIT0_DIM, MOON_CTR_VIDEO_TEX_H | (MOON_CTR_VIDEO_TEX_W << 16));
    C(GPUREG_TEXUNIT0_ADDR1, osConvertVirtToPhys(vramTex) >> 3);
    C(GPUREG_TEXUNIT0_PARAM,
      GPU_NEAREST | (GPU_LINEAR << 1)); // Linear min and mag filter

    // Shading
    // GPUCMD_AddMaskedWrite(GPUREG_SH_OUTATTR_CLOCK, 0x2, 1 << 8); // No Z, Yes
    // texcoord0
    C(GPUREG_TEXUNIT_CONFIG,
      1 | (1 << 12) | (1 << 16)); // Activate texture 0, clear texture cache

    C(GPUREG_TEXENV0_SOURCE, 0x003003); // Texture 0
    C(GPUREG_TEXENV0_OPERAND, 0);       // Source Color
    C(GPUREG_TEXENV0_COMBINER, 0);      // Replace
    C(GPUREG_TEXENV0_SCALE, 0);         // No Scale

    C(GPUREG_TEXENV1_SOURCE, 0x003003); // Texture 0
    C(GPUREG_TEXENV1_OPERAND, 0);       // Source Color
    C(GPUREG_TEXENV1_COMBINER, 0);      // Replace
    C(GPUREG_TEXENV1_SCALE, 0);         // No Scale

    C(GPUREG_TEXENV2_SOURCE, 0x003003); // Texture 0
    C(GPUREG_TEXENV2_OPERAND, 0);       // Source Color
    C(GPUREG_TEXENV2_COMBINER, 0);      // Replace
    C(GPUREG_TEXENV2_SCALE, 0);         // No Scale

    C(GPUREG_TEXENV3_SOURCE, 0x003003); // Texture 0
    C(GPUREG_TEXENV3_OPERAND, 0);       // Source Color
    C(GPUREG_TEXENV3_COMBINER, 0);      // Replace
    C(GPUREG_TEXENV3_SCALE, 0);         // No Scale

    C(GPUREG_TEXENV4_SOURCE, 0x003003); // Texture 0
    C(GPUREG_TEXENV4_OPERAND, 0);       // Source Color
    C(GPUREG_TEXENV4_COMBINER, 0);      // Replace
    C(GPUREG_TEXENV4_SCALE, 0);         // No Scale

    C(GPUREG_TEXENV5_SOURCE, 0x003003); // Texture 0
    C(GPUREG_TEXENV5_OPERAND, 0);       // Source Color
    C(GPUREG_TEXENV5_COMBINER, 0);      // Replace
    C(GPUREG_TEXENV5_SCALE, 0);         // No Scale

    // Attribute buffers
    C(GPUREG_ATTRIBBUFFERS_LOC, 0);
    C(GPUREG_ATTRIBBUFFERS_FORMAT_LOW, 0);
    C(GPUREG_ATTRIBBUFFERS_FORMAT_HIGH,
      (0xFFF << 16) | (1 << 28)); // Two fixed vertex attributes

    // Vertex Shader
    static DVLB_s *vshader_dvlb = NULL;
    static shaderProgram_s program;

    if (!vshader_dvlb) {
        vshader_dvlb = DVLB_ParseFile((u32 *)vshader_shbin, vshader_shbin_size);
        shaderProgramInit(&program);
        shaderProgramSetVsh(&program, &vshader_dvlb->DVLE[0]);
    }

    shaderProgramUse(&program);

    C(GPUREG_VSH_NUM_ATTR, 1); // 2 attributes
    GPUCMD_AddMaskedWrite(GPUREG_VSH_INPUTBUFFER_CONFIG, 0xB,
                          1 | (0xA0 << 24)); // 2 attributes, no geometry shader
    C(GPUREG_VSH_ATTRIBUTES_PERMUTATION_LOW, 0x00000010);
    C(GPUREG_VSH_ATTRIBUTES_PERMUTATION_HIGH, 0);

    // Geometry Pipeline
    C(GPUREG_FACECULLING_CONFIG, 0);
    C(GPUREG_GEOSTAGE_CONFIG, 0);
    GPUCMD_AddMaskedWrite(GPUREG_PRIMITIVE_CONFIG, 2,
                          (1 << 8) |
                              1); // 2 outmap registers, drawing triangle strip
    C(GPUREG_INDEXBUFFER_CONFIG, 0x80000000);

    union {
        u32 packed[3];
        struct {
            u8 x[3], y[3], z[3], w[3];
        };
    } param;

#define ATTR(X, Y, Z, W)                                                       \
    {                                                                          \
        write24(param.x, f32tof24(X));                                         \
        write24(param.y, f32tof24(Y));                                         \
        write24(param.z, f32tof24(Z));                                         \
        write24(param.w, f32tof24(W));                                         \
                                                                               \
        u32 p = param.packed[0];                                               \
        param.packed[0] = param.packed[2];                                     \
        param.packed[2] = p;                                                   \
        GPUCMD_AddIncrementalWrites(GPUREG_FIXEDATTRIB_DATA0, param.packed,    \
                                    3);                                        \
    }

    float sw = image_width / ((float)MOON_CTR_VIDEO_TEX_W);
    float sh = image_height / ((float)MOON_CTR_VIDEO_TEX_H);

    // float hw = 2.0f / surface_height;
    float hh = 2.0f / surface_width;

    // One immediate-mode quad. Clip-space y runs from the screen's right edge
    // (-1, the end of vramFb) to its left edge (+1, the start); the quad
    // covers y_right..y_left and shows texture columns s_left..s_right of the
    // picture, so the 2D quad is (-1, +1, 0, sw).
    auto draw_quad = [&](float y_right, float y_left, float s_left,
                         float s_right) {
        C(GPUREG_RESTART_PRIMITIVE, 1);
        GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG2, 1, 1);
        GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 1, 0);
        C(GPUREG_FIXEDATTRIB_INDEX, 0xF);

        ATTR(1.0, y_right, 0.0, 0.0); // TR
        ATTR(s_right, -hh, 0.0, 0.0);

        ATTR(-1.0, y_right, 0.0, 0.0); // TL
        ATTR(s_right, sh, 0.0, 0.0);

        ATTR(1.0, y_left, 0.0, 0.0); // BR
        ATTR(s_left, -hh, 0.0, 0.0);

        ATTR(-1.0, y_left, 0.0, 0.0); // BL
        ATTR(s_left, sh, 0.0, 0.0);

        GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 1, 1);
        GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG2, 1, 0);
        C(GPUREG_VTX_FUNC, 1);
    };

    if (stereo_on && stereo.full_frame) {
        // Flat depth: each eye gets the whole picture minus a sliver, the left
        // eye losing the left edge and the right eye the right edge. Every
        // point then sits a little further left for the left eye than for the
        // right one, which places the picture behind the screen.
        float crop = stereo.crop * sw;
        draw_quad(0.0f, 1.0f, crop, sw);         // left eye
        draw_quad(-1.0f, 0.0f, 0.0f, sw - crop); // right eye
    } else {
        // 2D, or side-by-side 3D where the halves are split up afterwards
        draw_quad(-1.0f, 1.0f, 0.0f, sw);
    }

    // Stop Command List
    GPUCMD_AddMaskedWrite(GPUREG_PRIMITIVE_CONFIG, 0x8, 0x00000000);
    C(GPUREG_FRAMEBUFFER_FLUSH, 1);
    C(GPUREG_FRAMEBUFFER_INVALIDATE, 1);

#undef C

    gspWaitForEvent(GSPGPU_EVENT_PPF, 0);

    u32 *unused;
    u32 cmdlist_len;
    GPUCMD_Split(&unused, &cmdlist_len);
    GSPGPU_FlushDataCache(cmdlist, cmdlist_len);

    extern u32 __ctru_linear_heap;
    extern u32 __ctru_linear_heap_size;
    GX_FlushCacheRegions(cmdlist, cmdlist_len * 4, (u32 *)__ctru_linear_heap,
                         __ctru_linear_heap_size, NULL, 0);

    GX_ProcessCommandList(cmdlist, cmdlist_len * 4, 2);

    gspWaitForEvent(GSPGPU_EVENT_P3D, 0);

    // Copy into framebuffer, untiled
    if (stereo_on && gfxIs3D()) {
        u32 *dest_left =
            (u32 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        u32 *dest_right =
            (u32 *)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
        if (stereo.swap) {
            std::swap(dest_left, dest_right);
        }
        const int eye_rows = rt_rows / 2;
        const u32 eye_offset =
            surface_height * eye_rows * px_size / sizeof(u32);

        // First half of vramFb: the left-eye picture
        GX_DisplayTransfer((u32 *)vramFb,
                           GX_BUFFER_DIM(surface_height, eye_rows), dest_left,
                           GX_BUFFER_DIM(surface_height, eye_rows),
                           GX_TRANSFER_OUT_TILED(0) |
                               GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
                               GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                               GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
        // Wait for each transfer separately. A single wait for two transfers
        // can return after the first one, which leaves the second one's
        // completion pending and makes the next frame's wait for the texture
        // upload return before that upload has finished.
        gspWaitForEvent(GSPGPU_EVENT_PPF, 0);

        // Second half: the right-eye picture
        GX_DisplayTransfer((u32 *)vramFb + eye_offset,
                           GX_BUFFER_DIM(surface_height, eye_rows), dest_right,
                           GX_BUFFER_DIM(surface_height, eye_rows),
                           GX_TRANSFER_OUT_TILED(0) |
                               GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
                               GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                               GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    } else {
        u32 *dest = (u32 *)gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL);
        GX_DisplayTransfer((u32 *)vramFb,
                           GX_BUFFER_DIM(surface_height, surface_width), dest,
                           GX_BUFFER_DIM(surface_height, surface_width),
                           GX_TRANSFER_OUT_TILED(0) |
                               GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
                               GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                               GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    }
    gspWaitForEvent(GSPGPU_EVENT_PPF, 0);

    perf_fbcopy_ticks = svcGetSystemTick() - start_ticks;
    if (debug) {
        log_perf_counters();
    }

    gfxScreenSwapBuffers(screen, true);
}
