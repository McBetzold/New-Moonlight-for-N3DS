/*
 * This file is part of Moonlight Embedded.
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

#include "ui_gfx.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ui {

static bool g_ready = false;
static C3D_RenderTarget *g_top = nullptr;
static C3D_RenderTarget *g_bottom = nullptr;
static C2D_TextBuf g_text_buf = nullptr;
static C2D_TextBuf g_measure_buf = nullptr;

u32 with_alpha(u32 color, u8 alpha) {
    return (color & 0x00FFFFFFu) | ((u32)alpha << 24);
}

u32 mix(u32 a, u32 b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    u32 out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        float ca = (float)((a >> shift) & 0xFF);
        float cb = (float)((b >> shift) & 0xFF);
        u32 c = (u32)(ca + (cb - ca) * t + 0.5f);
        out |= (c & 0xFF) << shift;
    }
    return out;
}

bool gfx_init() {
    if (g_ready) {
        return true;
    }
    // A previous stream may have left the top screen in stereoscopic mode.
    gfxSet3D(false);

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        return false;
    }
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        C3D_Fini();
        return false;
    }
    C2D_Prepare();
    fontEnsureMapped();

    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    if (!g_top || !g_bottom) {
        if (g_top)
            C3D_RenderTargetDelete(g_top);
        if (g_bottom)
            C3D_RenderTargetDelete(g_bottom);
        g_top = g_bottom = nullptr;
        C2D_Fini();
        C3D_Fini();
        return false;
    }

    // The framebuffers are RGB565 (see gfxInit in n3ds_main.cpp), so the
    // display transfer must convert to that format instead of the RGB8
    // output citro2d sets up by default.
    const u32 transfer_flags =
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
        GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    C3D_RenderTargetSetOutput(g_top, GFX_TOP, GFX_LEFT, transfer_flags);
    C3D_RenderTargetSetOutput(g_bottom, GFX_BOTTOM, GFX_LEFT, transfer_flags);

    g_text_buf = C2D_TextBufNew(8192);
    g_measure_buf = C2D_TextBufNew(1024);
    g_ready = true;
    return true;
}

void gfx_fini() {
    if (!g_ready) {
        return;
    }
    if (g_text_buf) {
        C2D_TextBufDelete(g_text_buf);
        g_text_buf = nullptr;
    }
    if (g_measure_buf) {
        C2D_TextBufDelete(g_measure_buf);
        g_measure_buf = nullptr;
    }
    C3D_RenderTargetDelete(g_top);
    C3D_RenderTargetDelete(g_bottom);
    g_top = g_bottom = nullptr;
    C2D_Fini();
    C3D_Fini();
    g_ready = false;
}

bool gfx_ready() { return g_ready; }

void frame_begin() {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(g_text_buf);
}

void scene(Screen screen, u32 clear_color) {
    C3D_RenderTarget *target = screen == Screen::Top ? g_top : g_bottom;
    C2D_TargetClear(target, clear_color);
    C2D_SceneBegin(target);
}

void frame_end() { C3D_FrameEnd(0); }

// ---- Text -----------------------------------------------------------------

static bool parse_text(C2D_Text &text, C2D_TextBuf buf,
                       const std::string &str) {
    return C2D_TextParse(&text, buf, str.c_str()) != nullptr;
}

float text_width(const std::string &str, float scale) {
    if (str.empty() || !g_ready) {
        return 0.0f;
    }
    C2D_TextBufClear(g_measure_buf);
    C2D_Text t;
    if (!parse_text(t, g_measure_buf, str)) {
        return 0.0f;
    }
    float w = 0.0f, h = 0.0f;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

void text(const std::string &str, float x, float y, float scale, u32 color,
          u32 align) {
    if (str.empty() || !g_ready) {
        return;
    }
    C2D_Text t;
    if (!parse_text(t, g_text_buf, str)) {
        return;
    }
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | align, x, y, 0.0f, scale, scale, color);
}

static size_t utf8_prev(const std::string &s, size_t pos) {
    if (pos == 0) {
        return 0;
    }
    pos--;
    while (pos > 0 && (((unsigned char)s[pos]) & 0xC0) == 0x80) {
        pos--;
    }
    return pos;
}

static size_t utf8_next(const std::string &s, size_t pos) {
    if (pos >= s.size()) {
        return s.size();
    }
    pos++;
    while (pos < s.size() && (((unsigned char)s[pos]) & 0xC0) == 0x80) {
        pos++;
    }
    return pos;
}

std::string ellipsize(const std::string &str, float scale, float max_width) {
    if (text_width(str, scale) <= max_width) {
        return str;
    }
    static const std::string ellipsis = "\xE2\x80\xA6";
    std::string cur = str;
    while (!cur.empty()) {
        cur.erase(utf8_prev(cur, cur.size()));
        std::string candidate = cur + ellipsis;
        if (text_width(candidate, scale) <= max_width) {
            return candidate;
        }
    }
    return ellipsis;
}

std::vector<std::string> wrap(const std::string &str, float scale,
                              float max_width) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= str.size()) {
        size_t nl = str.find('\n', start);
        std::string para =
            str.substr(start, nl == std::string::npos ? std::string::npos
                                                      : nl - start);
        std::string line;
        size_t p = 0;
        while (p < para.size()) {
            size_t sp = para.find(' ', p);
            std::string word =
                para.substr(p, sp == std::string::npos ? std::string::npos
                                                       : sp - p);
            p = (sp == std::string::npos) ? para.size() : sp + 1;
            if (word.empty()) {
                continue;
            }
            std::string candidate = line.empty() ? word : line + " " + word;
            if (text_width(candidate, scale) <= max_width) {
                line = candidate;
                continue;
            }
            if (!line.empty()) {
                out.push_back(line);
                line.clear();
            }
            if (text_width(word, scale) <= max_width) {
                line = word;
                continue;
            }
            // A single word wider than the line: break it by character.
            std::string piece;
            size_t i = 0;
            while (i < word.size()) {
                size_t next = utf8_next(word, i);
                std::string grown = piece + word.substr(i, next - i);
                if (!piece.empty() && text_width(grown, scale) > max_width) {
                    out.push_back(piece);
                    grown = word.substr(i, next - i);
                }
                piece = grown;
                i = next;
            }
            line = piece;
        }
        out.push_back(line);
        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }
    return out;
}

float text_block(const std::vector<std::string> &lines, float x, float y,
                 float scale, u32 color, u32 align, float line_gap) {
    float lh = line_height(scale) + line_gap;
    float cy = y;
    for (const auto &l : lines) {
        text(l, x, cy, scale, color, align);
        cy += lh;
    }
    return cy - y;
}

// ---- Shapes ---------------------------------------------------------------

void rect(float x, float y, float w, float h, u32 color) {
    if (w <= 0 || h <= 0)
        return;
    C2D_DrawRectSolid(x, y, 0.0f, w, h, color);
}

void rect_gradient_v(float x, float y, float w, float h, u32 top, u32 bottom) {
    C2D_DrawRectangle(x, y, 0.0f, w, h, top, top, bottom, bottom);
}

void rect_outline(float x, float y, float w, float h, float thickness,
                  u32 color) {
    rect(x, y, w, thickness, color);
    rect(x, y + h - thickness, w, thickness, color);
    rect(x, y + thickness, thickness, h - 2 * thickness, color);
    rect(x + w - thickness, y + thickness, thickness, h - 2 * thickness,
         color);
}

void rrect(float x, float y, float w, float h, float radius, u32 color) {
    radius = std::min(radius, std::min(w, h) / 2.0f);
    if (radius <= 0.5f) {
        rect(x, y, w, h, color);
        return;
    }
    rect(x + radius, y, w - 2 * radius, h, color);
    rect(x, y + radius, radius, h - 2 * radius, color);
    rect(x + w - radius, y + radius, radius, h - 2 * radius, color);
    circle(x + radius, y + radius, radius, color);
    circle(x + w - radius, y + radius, radius, color);
    circle(x + radius, y + h - radius, radius, color);
    circle(x + w - radius, y + h - radius, radius, color);
}

void rrect_outline(float x, float y, float w, float h, float radius,
                   float thickness, u32 color) {
    // Straight edges inset by the corner radius plus a few circular caps
    // along each corner arc; good enough for focus rings.
    radius = std::min(radius, std::min(w, h) / 2.0f);
    rect(x + radius, y, w - 2 * radius, thickness, color);
    rect(x + radius, y + h - thickness, w - 2 * radius, thickness, color);
    rect(x, y + radius, thickness, h - 2 * radius, color);
    rect(x + w - thickness, y + radius, thickness, h - 2 * radius, color);
    const float caps[4][2] = {{x + radius, y + radius},
                              {x + w - radius, y + radius},
                              {x + radius, y + h - radius},
                              {x + w - radius, y + h - radius}};
    for (auto &c : caps) {
        float cx = c[0], cy = c[1];
        float sx = (cx < x + w / 2) ? -1.0f : 1.0f;
        float sy = (cy < y + h / 2) ? -1.0f : 1.0f;
        for (int i = 0; i <= 4; i++) {
            float a = (float)M_PI_2 * (float)i / 4.0f;
            float px = cx + sx * (radius - thickness / 2) * std::cos(a);
            float py = cy + sy * (radius - thickness / 2) * std::sin(a);
            circle(px, py, thickness / 2, color);
        }
    }
}

void circle(float cx, float cy, float radius, u32 color) {
    if (radius <= 0)
        return;
    C2D_DrawCircleSolid(cx, cy, 0.0f, radius, color);
}

void ring(float cx, float cy, float radius, float thickness, u32 color,
          u32 hole_color) {
    circle(cx, cy, radius, color);
    circle(cx, cy, radius - thickness, hole_color);
}

void line(float x0, float y0, float x1, float y1, float thickness,
          u32 color) {
    C2D_DrawLine(x0, y0, color, x1, y1, color, thickness, 0.0f);
}

void triangle(float x0, float y0, float x1, float y1, float x2, float y2,
              u32 color) {
    C2D_DrawTriangle(x0, y0, color, x1, y1, color, x2, y2, color, 0.0f);
}

float pill(float x, float y, const std::string &label, float scale, u32 fill,
           u32 text_color, float pad_x) {
    float w = text_width(label, scale) + 2 * pad_x;
    float h = line_height(scale) + 4.0f;
    rrect(x, y, w, h, h / 2.0f, fill);
    text(label, x + w / 2.0f, y + 2.0f, scale, text_color, C2D_AlignCenter);
    return w;
}

// ---- Icons ----------------------------------------------------------------

namespace icon {

void moon(float cx, float cy, float r, u32 color, u32 bg) {
    circle(cx, cy, r, color);
    circle(cx + r * 0.42f, cy - r * 0.30f, r * 0.80f, bg);
    // a small star
    circle(cx + r * 0.55f, cy + r * 0.45f, r * 0.10f, color);
}

void desktop(float cx, float cy, float size, u32 color) {
    float w = size;
    float h = size * 0.62f;
    float top = cy - size * 0.46f;
    rrect(cx - w / 2, top, w, h, size * 0.06f, color);
    // neck and base
    rect(cx - size * 0.07f, top + h, size * 0.14f, size * 0.14f, color);
    rrect(cx - size * 0.28f, top + h + size * 0.14f, size * 0.56f,
          size * 0.07f, size * 0.03f, color);
}

void gear(float cx, float cy, float r, u32 color, u32 bg) {
    const float hw = r * 0.24f;
    for (int i = 0; i < 8; i++) {
        float a = (float)i * (float)M_PI / 4.0f;
        float dx = std::cos(a), dy = std::sin(a);
        float px = -dy, py = dx;
        float ix = cx + dx * r * 0.68f, iy = cy + dy * r * 0.68f;
        float ox = cx + dx * r, oy = cy + dy * r;
        float x0 = ix + px * hw, y0 = iy + py * hw;
        float x1 = ox + px * hw * 0.7f, y1 = oy + py * hw * 0.7f;
        float x2 = ox - px * hw * 0.7f, y2 = oy - py * hw * 0.7f;
        float x3 = ix - px * hw, y3 = iy - py * hw;
        triangle(x0, y0, x1, y1, x2, y2, color);
        triangle(x0, y0, x2, y2, x3, y3, color);
    }
    circle(cx, cy, r * 0.76f, color);
    circle(cx, cy, r * 0.34f, bg);
}

void plus(float cx, float cy, float size, float thickness, u32 color) {
    rect(cx - thickness / 2, cy - size / 2, thickness, size, color);
    rect(cx - size / 2, cy - thickness / 2, size, thickness, color);
}

void lock(float cx, float cy, float size, u32 color, u32 bg) {
    ring(cx, cy - size * 0.12f, size * 0.27f, size * 0.10f, color, bg);
    float bw = size * 0.74f, bh = size * 0.52f;
    rrect(cx - bw / 2, cy - size * 0.04f, bw, bh, size * 0.08f, color);
    circle(cx, cy + size * 0.20f, size * 0.07f, bg);
}

void warning(float cx, float cy, float size, u32 color, u32 mark) {
    triangle(cx, cy - size / 2, cx - size / 2, cy + size * 0.45f,
             cx + size / 2, cy + size * 0.45f, color);
    rect(cx - size * 0.06f, cy - size * 0.15f, size * 0.12f, size * 0.33f,
         mark);
    circle(cx, cy + size * 0.30f, size * 0.07f, mark);
}

void play(float cx, float cy, float size, u32 color) {
    triangle(cx - size * 0.38f, cy - size / 2, cx - size * 0.38f,
             cy + size / 2, cx + size * 0.55f, cy, color);
}

void stop(float cx, float cy, float size, u32 color) {
    rrect(cx - size / 2, cy - size / 2, size, size, size * 0.12f, color);
}

void refresh(float cx, float cy, float r, u32 color, u32 bg) {
    ring(cx, cy, r, r * 0.30f, color, bg);
    // Cut a gap in the upper-right quadrant and put an arrow head on it.
    triangle(cx, cy, cx + r * 1.3f, cy - r * 1.3f, cx + r * 1.3f, cy, bg);
    triangle(cx + r * 0.45f, cy - r * 1.15f, cx + r * 1.25f, cy - r * 0.55f,
             cx + r * 0.40f, cy - r * 0.35f, color);
}

void back_arrow(float cx, float cy, float size, u32 color) {
    triangle(cx - size / 2, cy, cx - size * 0.05f, cy - size * 0.45f,
             cx - size * 0.05f, cy + size * 0.45f, color);
    rect(cx - size * 0.12f, cy - size * 0.15f, size * 0.62f, size * 0.30f,
         color);
}

void check(float cx, float cy, float size, u32 color) {
    float t = size * 0.18f;
    line(cx - size * 0.45f, cy + size * 0.02f, cx - size * 0.12f,
         cy + size * 0.35f, t, color);
    line(cx - size * 0.12f, cy + size * 0.35f, cx + size * 0.50f,
         cy - size * 0.40f, t, color);
}

void cross(float cx, float cy, float size, u32 color) {
    float t = size * 0.16f;
    float h = size * 0.40f;
    line(cx - h, cy - h, cx + h, cy + h, t, color);
    line(cx - h, cy + h, cx + h, cy - h, t, color);
}

void dots(float cx, float cy, float size, u32 color) {
    float r = size * 0.11f;
    circle(cx, cy - size * 0.33f, r, color);
    circle(cx, cy, r, color);
    circle(cx, cy + size * 0.33f, r, color);
}

void gamepad(float cx, float cy, float size, u32 color, u32 detail) {
    float w = size, h = size * 0.56f;
    rrect(cx - w / 2, cy - h / 2, w, h, size * 0.16f, color);
    circle(cx - w * 0.32f, cy + h * 0.25f, size * 0.14f, color);
    circle(cx + w * 0.32f, cy + h * 0.25f, size * 0.14f, color);
    // d-pad
    plus(cx - w * 0.26f, cy - h * 0.05f, size * 0.22f, size * 0.07f, detail);
    // face buttons
    circle(cx + w * 0.20f, cy - h * 0.05f, size * 0.045f, detail);
    circle(cx + w * 0.32f, cy - h * 0.25f, size * 0.045f, detail);
    circle(cx + w * 0.32f, cy + h * 0.15f, size * 0.045f, detail);
    circle(cx + w * 0.44f, cy - h * 0.05f, size * 0.045f, detail);
}

void wifi_off(float cx, float cy, float size, u32 color) {
    circle(cx, cy + size * 0.3f, size * 0.12f, color);
    cross(cx, cy, size * 0.9f, color);
}

void spinner(float cx, float cy, float r, u32 color, u64 time_ms) {
    const int dots = 8;
    int phase = (int)((time_ms / 90) % dots);
    float dot_r = std::max(1.5f, r * 0.22f);
    for (int i = 0; i < dots; i++) {
        int k = (i - phase + dots) % dots;
        float alpha = 1.0f - (float)k / (float)dots;
        float a = (float)i * 2.0f * (float)M_PI / (float)dots;
        float px = cx + std::cos(a) * r;
        float py = cy + std::sin(a) * r;
        circle(px, py, dot_r, with_alpha(color, (u8)(40 + 215 * alpha)));
    }
}

} // namespace icon

float hint(float x, float y, const char *button, const std::string &label,
           float scale, u32 label_color) {
    float lh = line_height(scale);
    float r = lh * 0.55f;
    float cx = x + r, cy = y + lh / 2.0f;
    circle(cx, cy, r, color::card_hi);
    float glyph_scale = scale * 0.85f;
    text(button, cx, cy - line_height(glyph_scale) / 2.0f, glyph_scale,
         color::text, C2D_AlignCenter);
    float lx = x + 2 * r + 4.0f;
    text(label, lx, y, scale, label_color);
    return lx + text_width(label, scale) + 14.0f;
}

} // namespace ui
