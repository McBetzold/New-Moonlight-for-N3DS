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
#include "vshader_shbin.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace ui {

static bool g_ready = false;
// See gfx_init(): a throwaway program used to force citro2d's shader to be
// re-uploaded after a C3D_Fini/C3D_Init cycle.
static DVLB_s *g_scratch_dvlb = nullptr;
static shaderProgram_s g_scratch_program;
static C3D_RenderTarget *g_top = nullptr;
static C3D_RenderTarget *g_bottom = nullptr;
static C2D_TextBuf g_text_buf = nullptr;
static C2D_TextBuf g_measure_buf = nullptr;
static u64 g_frame_time = 0;

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
    if (gfxIs3D()) {
        gfxSet3D(false);
    }

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        return false;
    }

    // C3D_Init clears the "program dirty" flag but keeps the pointer of the
    // program that was bound before C3D_Fini. citro2d binds the same static
    // program object again, so without this the shader is never re-sent and
    // the GPU keeps running the video renderer's vertex shader after a
    // stream. Binding a different (valid) program first makes citro2d's
    // rebind count as a change. gfx_fini() leaves the scratch program as the
    // remembered one, so on later calls this bind is a no-op and citro2d's
    // bind below is the change.
    if (!g_scratch_dvlb) {
        g_scratch_dvlb =
            DVLB_ParseFile((u32 *)vshader_shbin, vshader_shbin_size);
        if (g_scratch_dvlb) {
            shaderProgramInit(&g_scratch_program);
            shaderProgramSetVsh(&g_scratch_program, &g_scratch_dvlb->DVLE[0]);
        }
    }
    if (g_scratch_dvlb) {
        C3D_BindProgram(&g_scratch_program);
    }

    // The sky, rainbow frames and outlined lettering add a few hundred
    // objects per frame to pages that were already text heavy
    if (!C2D_Init(2 * C2D_DEFAULT_MAX_OBJECTS)) {
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
    // citro3d remembers the last bound program across C3D_Fini/C3D_Init and
    // compares the next bind against it, reading the old program's vertex
    // shader. C2D_Fini frees citro2d's shader instance but leaves its
    // program bound, so the first bind in gfx_init() after a stream read
    // freed memory, which crashed (data abort in C3D_BindProgram) whenever
    // the block had been reused, e.g. by the connection-terminated message.
    // Leave our scratch program, which is never freed, as the remembered one.
    if (g_scratch_dvlb) {
        C3D_BindProgram(&g_scratch_program);
    }
    C2D_Fini();
    C3D_Fini();
    g_ready = false;
}

// Once the system has asked the app to close (for example Close in the HOME
// Menu after the stream jumped there), libctru does not give the GPU back,
// so a new frame would wait for a VBlank that never comes and the close
// would hang forever. Nothing may be drawn after that point.
bool gfx_ready() { return g_ready && !aptShouldClose(); }

void frame_begin() {
    g_frame_time = osGetTime();
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
    // Three rectangles and four quarter-circle fans that do not overlap, so
    // translucent colours (panels over the sky) blend evenly
    rect(x + radius, y, w - 2 * radius, h, color);
    rect(x, y + radius, radius, h - 2 * radius, color);
    rect(x + w - radius, y + radius, radius, h - 2 * radius, color);
    const float corners[4][3] = {
        {x + radius, y + radius, (float)M_PI},
        {x + w - radius, y + radius, 1.5f * (float)M_PI},
        {x + w - radius, y + h - radius, 0.0f},
        {x + radius, y + h - radius, (float)M_PI_2}};
    const int steps = std::max(3, std::min(8, (int)radius / 2));
    for (auto &c : corners) {
        for (int i = 0; i < steps; i++) {
            float a0 = c[2] + (float)M_PI_2 * (float)i / (float)steps;
            float a1 = c[2] + (float)M_PI_2 * (float)(i + 1) / (float)steps;
            triangle(c[0], c[1], c[0] + radius * std::cos(a0),
                     c[1] + radius * std::sin(a0), c[0] + radius * std::cos(a1),
                     c[1] + radius * std::sin(a1), color);
        }
    }
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

// An opaque colour that looks like `c` over the sky: icons cut their holes
// with the colour behind them, and a translucent one would let the icon
// show through
static u32 solid(u32 c) {
    u8 a = (u8)(c >> 24);
    if (a == 0xFF) {
        return c;
    }
    u32 sky = mix(color::bg, color::sky_mid, 0.5f);
    return with_alpha(mix(sky, with_alpha(c, 0xFF), a / 255.0f), 0xFF);
}

void circle(float cx, float cy, float radius, u32 color) {
    if (radius <= 0)
        return;
    C2D_DrawCircleSolid(cx, cy, 0.0f, radius, color);
}

void ring(float cx, float cy, float radius, float thickness, u32 color,
          u32 hole_color) {
    circle(cx, cy, radius, color);
    circle(cx, cy, radius - thickness, solid(hole_color));
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

// ---- The New Moonlight look -------------------------------------------------

u64 frame_time() { return g_frame_time; }

// The wheel's colours: pink, orange, yellow, green, teal, blue, violet
static const u32 RAINBOW[] = {rgba(0xf0, 0x6a, 0x7a), rgba(0xf6, 0xa4, 0x50),
                              rgba(0xf6, 0xe0, 0x5a), rgba(0x7c, 0xd6, 0x6c),
                              rgba(0x5c, 0xc8, 0xe0), rgba(0x6a, 0x8c, 0xf0),
                              rgba(0xb0, 0x78, 0xf0)};
static const int RAINBOW_N = sizeof(RAINBOW) / sizeof(RAINBOW[0]);

u32 rainbow(float t) {
    t -= std::floor(t);
    float f = t * RAINBOW_N;
    int i = std::min(RAINBOW_N - 1, (int)f);
    return mix(RAINBOW[i], RAINBOW[(i + 1) % RAINBOW_N], f - (float)i);
}

// How far into a repeating period of period_ms the frame is (0..1). Kept
// small on purpose: osGetTime() counts from 1900, far beyond what a float
// holds to the millisecond.
static float cycle(u64 period_ms) {
    return (float)(g_frame_time % period_ms) / (float)period_ms;
}

namespace {
struct Star {
    float x, y, r, phase, rate;
    bool sparkle;
};
struct Wisp {
    float cx, cy, rx, ry;
    u32 color;
};
} // namespace

static const u32 NEBULA_PINK = rgba(0xe7, 0xa6, 0xd7);
static const u32 NEBULA_LAVENDER = rgba(0xb8, 0xa6, 0xe8);
static const u32 NEBULA_TEAL = rgba(0x7f, 0xd3, 0xd8);
// as on the banner: faint wisps near the corners
static const Wisp WISPS_TOP[] = {{70, 66, 84, 50, NEBULA_PINK},
                                 {345, 66, 72, 44, NEBULA_LAVENDER},
                                 {60, 194, 72, 42, NEBULA_TEAL},
                                 {340, 196, 84, 48, NEBULA_PINK}};
static const Wisp WISPS_BOTTOM[] = {{56, 70, 72, 44, NEBULA_LAVENDER},
                                    {272, 190, 80, 46, NEBULA_TEAL},
                                    {250, 56, 60, 36, NEBULA_PINK}};

static std::vector<Star> make_stars(float w, float h, int dots, int sparkles,
                                    u32 seed) {
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (float)(seed >> 8) / (float)(1u << 24);
    };
    std::vector<Star> stars;
    for (int i = 0; i < dots + sparkles; i++) {
        Star s;
        s.sparkle = i >= dots;
        s.x = rnd() * w;
        s.y = rnd() * h;
        s.r = s.sparkle ? 3.0f + rnd() * 2.5f : 0.7f + rnd() * 0.6f;
        s.phase = rnd();
        s.rate = 0.15f + rnd() * 0.35f; // twinkles per second
        stars.push_back(s);
    }
    return stars;
}

void sky(Screen screen) {
    static const std::vector<Star> top_stars =
        make_stars(TOP_W, TOP_H, 48, 6, 21);
    static const std::vector<Star> bottom_stars =
        make_stars(BOT_W, BOT_H, 36, 4, 5);
    bool top = screen == Screen::Top;
    float w = top ? TOP_W : BOT_W;
    float h = top ? TOP_H : BOT_H;

    // darkest at the top and bottom edges, deep indigo across the middle
    float mid = h * 0.55f;
    rect_gradient_v(0, 0, w, mid, color::bg, color::sky_mid);
    rect_gradient_v(0, mid, w, h - mid, color::sky_mid, color::bg);

    // nebula wisps: stacked faint ellipses, brightest in the middle
    const Wisp *wisps = top ? WISPS_TOP : WISPS_BOTTOM;
    int wisp_count = top ? (int)(sizeof(WISPS_TOP) / sizeof(Wisp))
                         : (int)(sizeof(WISPS_BOTTOM) / sizeof(Wisp));
    for (int i = 0; i < wisp_count; i++) {
        const Wisp &n = wisps[i];
        for (int k = 0; k < 6; k++) {
            float f = 1.0f - 0.16f * (float)k;
            C2D_DrawEllipseSolid(n.cx - n.rx * f, n.cy - n.ry * f, 0.0f,
                                 2 * n.rx * f, 2 * n.ry * f,
                                 with_alpha(n.color, 7));
        }
    }

    // twinkling stars; a few are four-pointed sparkles like the banner's
    float t = (float)(g_frame_time % 600000) / 1000.0f;
    for (const Star &s : top ? top_stars : bottom_stars) {
        float b = 0.5f + 0.5f * std::sin(2.0f * (float)M_PI *
                                         (t * s.rate + s.phase));
        if (!s.sparkle) {
            circle(s.x, s.y, s.r,
                   with_alpha(color::silver, (u8)(60 + 160 * b)));
            continue;
        }
        float len = s.r * (0.45f + 0.55f * b);
        u32 c = with_alpha(color::white, (u8)(40 + 200 * b));
        circle(s.x, s.y, len * 0.7f,
               with_alpha(color::accent_text, (u8)(24 * b)));
        line(s.x - len, s.y, s.x + len, s.y, 1.0f, c);
        line(s.x, s.y - len, s.x, s.y + len, 1.0f, c);
        circle(s.x, s.y, 1.1f, c);
    }
}

void panel(float x, float y, float w, float h, float radius) {
    rrect(x, y, w, h, radius, color::glass);
    // a metallic highlight along the top edge
    rect(x + radius, y, w - 2 * radius, 1, with_alpha(color::white, 0x38));
}

void rainbow_bar(float x, float y, float w, float h) {
    const int segments = 14;
    float phase = cycle(12000);
    for (int i = 0; i < segments; i++) {
        float x0 = x + w * (float)i / segments;
        float x1 = x + w * (float)(i + 1) / segments;
        u32 c0 = rainbow(phase + (float)i / segments);
        u32 c1 = rainbow(phase + (float)(i + 1) / segments);
        C2D_DrawRectangle(x0, y, 0.0f, x1 - x0, h, c0, c1, c0, c1);
    }
}

void rainbow_frame(float x, float y, float w, float h, float radius,
                   float thickness) {
    // the outline's centre line: a rounded rectangle inset by half the
    // thickness, walked clockwise from the top-left corner
    float half = thickness / 2.0f;
    x += half;
    y += half;
    w -= thickness;
    h -= thickness;
    radius = std::max(0.0f, std::min(radius - half, std::min(w, h) / 2.0f));
    std::vector<std::pair<float, float>> pts;
    pts.reserve(64);
    auto corner = [&](float cx, float cy, float a0) {
        for (int i = 0; i <= 4; i++) {
            float a = a0 + (float)M_PI_2 * (float)i / 4.0f;
            pts.push_back(
                {cx + radius * std::cos(a), cy + radius * std::sin(a)});
        }
    };
    auto edge = [&](float x0, float y0, float x1, float y1) {
        int n = std::max(1, (int)(std::hypot(x1 - x0, y1 - y0) / 14.0f));
        for (int i = 1; i < n; i++) {
            float k = (float)i / (float)n;
            pts.push_back({x0 + (x1 - x0) * k, y0 + (y1 - y0) * k});
        }
    };
    corner(x + radius, y + radius, (float)M_PI);
    edge(x + radius, y, x + w - radius, y);
    corner(x + w - radius, y + radius, 1.5f * (float)M_PI);
    edge(x + w, y + radius, x + w, y + h - radius);
    corner(x + w - radius, y + h - radius, 0.0f);
    edge(x + w - radius, y + h, x + radius, y + h);
    corner(x + radius, y + h - radius, (float)M_PI_2);
    edge(x, y + h - radius, x, y + radius);

    size_t n = pts.size();
    std::vector<float> along(n + 1, 0.0f);
    for (size_t i = 0; i < n; i++) {
        const auto &a = pts[i];
        const auto &b = pts[(i + 1) % n];
        along[i + 1] =
            along[i] + std::hypot(b.first - a.first, b.second - a.second);
    }
    if (along[n] <= 0.0f) {
        return;
    }
    float phase = cycle(8000);
    for (size_t i = 0; i < n; i++) {
        const auto &a = pts[i];
        const auto &b = pts[(i + 1) % n];
        u32 c0 = rainbow(phase + along[i] / along[n]);
        u32 c1 = rainbow(phase + along[i + 1] / along[n]);
        // round joints, so the corners have no gaps
        circle(a.first, a.second, half, c0);
        if (along[i + 1] - along[i] > 0.01f) {
            C2D_DrawLine(a.first, a.second, c0, b.first, b.second, c1,
                         thickness, 0.0f);
        }
    }
}

// A ring between radii inner and outer in the rainbow, turned by angle (the
// colours stay fixed to the ring as it turns); colour_shift moves the
// colours around a ring that stays still instead
static void rainbow_ring(float cx, float cy, float outer, float inner,
                         float angle, float colour_shift = 0.0f) {
    const float two_pi = 2.0f * (float)M_PI;
    const int segments = outer >= 20.0f ? 48 : 24;
    for (int i = 0; i < segments; i++) {
        float t0 = (float)i / segments, t1 = (float)(i + 1) / segments;
        float a0 = angle + two_pi * t0, a1 = angle + two_pi * t1;
        u32 c0 = rainbow(t0 - colour_shift), c1 = rainbow(t1 - colour_shift);
        float ox0 = cx + outer * std::cos(a0), oy0 = cy + outer * std::sin(a0);
        float ox1 = cx + outer * std::cos(a1), oy1 = cy + outer * std::sin(a1);
        float ix0 = cx + inner * std::cos(a0), iy0 = cy + inner * std::sin(a0);
        float ix1 = cx + inner * std::cos(a1), iy1 = cy + inner * std::sin(a1);
        C2D_DrawTriangle(ox0, oy0, c0, ox1, oy1, c1, ix1, iy1, c1, 0.0f);
        C2D_DrawTriangle(ox0, oy0, c0, ix1, iy1, c1, ix0, iy0, c0, 0.0f);
    }
}

void wheel(float cx, float cy, float r, float angle) {
    const float two_pi = 2.0f * (float)M_PI;
    const float inner = r * 0.80f;
    rainbow_ring(cx, cy, r, inner, angle);
    // eight spokes, silver at the hub and the ring's colour where they meet
    // it
    float thickness = std::max(1.5f, r * 0.11f);
    for (int k = 0; k < 8; k++) {
        float t = (float)k / 8.0f;
        float a = angle + two_pi * t;
        float dx = std::cos(a), dy = std::sin(a);
        C2D_DrawLine(cx + dx * r * 0.18f, cy + dy * r * 0.18f, color::silver,
                     cx + dx * inner * 1.02f, cy + dy * inner * 1.02f,
                     rainbow(t), thickness, 0.0f);
    }
    circle(cx, cy, r * 0.22f, color::silver);
    circle(cx, cy, r * 0.12f, mix(color::silver, color::sky_mid, 0.45f));
}

void logo_wheel(float cx, float cy, float r) {
    wheel(cx, cy, r, 2.0f * (float)M_PI * cycle(20000));
}

static const u32 EDGE_DARK = rgba(0x08, 0x08, 0x18);
static const float AROUND[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0},
                                   {1, 0},   {-1, 1}, {0, 1},  {1, 1}};

void silver_text(const std::string &str, float x, float y, float scale,
                 u32 align) {
    float d = std::max(1.0f, scale * 1.6f);
    for (auto &o : AROUND) {
        text(str, x + o[0] * d, y + o[1] * d, scale, EDGE_DARK, align);
    }
    text(str, x, y, scale, color::silver, align);
}

static float logo_edge(float scale) { return std::max(1.0f, scale * 1.8f); }

float logo_text_width(const std::string &str, float scale) {
    return text_width(str, scale) + 4.0f * logo_edge(scale);
}

float logo_text(const std::string &str, float x, float y, float scale,
                int red_index) {
    float d = logo_edge(scale);
    float cx = x + 2.0f * d;
    int letter = 0;
    for (size_t i = 0; i < str.size();) {
        size_t next = utf8_next(str, i);
        std::string ch = str.substr(i, next - i);
        float w = text_width(ch, scale);
        if (ch != " ") {
            u32 outline = rainbow((float)letter / RAINBOW_N);
            for (auto &o : AROUND) {
                text(ch, cx + o[0] * 2.0f * d, y + o[1] * 2.0f * d, scale,
                     outline);
            }
            for (auto &o : AROUND) {
                text(ch, cx + o[0] * d, y + o[1] * d, scale, EDGE_DARK);
            }
            text(ch, cx, y, scale,
                 (int)i == red_index ? color::red : color::silver);
            letter++;
        }
        cx += w;
        i = next;
    }
    return cx + 2.0f * d - x;
}

// ---- Icons ----------------------------------------------------------------

namespace icon {

void moon(float cx, float cy, float r, u32 color, u32 bg) {
    bg = solid(bg);
    circle(cx, cy, r, color);
    circle(cx + r * 0.42f, cy - r * 0.30f, r * 0.80f, bg);
    // a small star
    circle(cx + r * 0.55f, cy + r * 0.45f, r * 0.10f, color);
}

// The monitor's stand, under a screen of height h starting at top
static void monitor_stand(float cx, float top, float h, float size,
                          u32 color) {
    rect(cx - size * 0.07f, top + h, size * 0.14f, size * 0.14f, color);
    rrect(cx - size * 0.28f, top + h + size * 0.14f, size * 0.56f,
          size * 0.07f, size * 0.03f, color);
}

void desktop(float cx, float cy, float size, u32 color, bool lit) {
    float w = size;
    float h = size * 0.62f;
    float top = cy - size * 0.46f;
    float b = std::max(2.0f, size * 0.07f); // the frame
    rrect(cx - w / 2, top, w, h, size * 0.06f, color);
    // the screen: a night sky, with a rainbow taskbar and stars when lit
    float sx = cx - w / 2 + b, sy = top + b, sw = w - 2 * b, sh = h - 2 * b;
    if (lit) {
        rect_gradient_v(sx, sy, sw, sh, color::sky_mid, color::bg);
        float bar = std::max(1.5f, size * 0.05f);
        rainbow_bar(sx, sy + sh - bar, sw, bar);
        if (size >= 50.0f) {
            const float stars[3][2] = {{0.2f, 0.25f}, {0.72f, 0.2f},
                                       {0.48f, 0.55f}};
            for (auto &s : stars) {
                circle(sx + sw * s[0], sy + sh * s[1], 1.0f,
                       with_alpha(color::silver, 0xb0));
            }
        }
    } else {
        rect(sx, sy, sw, sh, color::bg);
    }
    monitor_stand(cx, top, h, size, color);
}

void virtual_display(float cx, float cy, float size) {
    float w = size;
    float h = size * 0.62f;
    float top = cy - size * 0.46f;
    float b = std::max(2.0f, size * 0.07f);
    rect_gradient_v(cx - w / 2 + b, top + b, w - 2 * b, h - 2 * b,
                    color::sky_mid, color::bg);
    rainbow_frame(cx - w / 2, top, w, h, size * 0.08f, b);
    plus(cx, top + h / 2, h * 0.45f, b, color::silver);
    monitor_stand(cx, top, h, size, color::silver);
}

void steam(float cx, float cy, float r, u32 cut) {
    // a thin rainbow ring, the launcher's touch: it stays still while its
    // colours flow around it
    rainbow_ring(cx, cy, r * 1.13f, r * 1.04f, 0.0f, cycle(20000));
    // Steam's logo, measured from its default picture: a white disc with
    // a piston (a large and a small ring joined by a rod, and a tail out
    // to the disc's left edge) cut out in the background colour
    circle(cx, cy, r, color::text);
    float bx = cx + r * 0.385f, by = cy - r * 0.26f; // large ring
    float sx = cx - r * 0.31f, sy = cy + r * 0.39f;  // small ring
    // the tail, ending on the disc's edge
    float ax = cx - r * 0.997f, ay = cy - r * 0.08f;
    float dx = cx - r * 0.975f, dy = cy + r * 0.22f;
    triangle(ax, ay, sx, sy - r * 0.25f, sx, sy + r * 0.25f, cut);
    triangle(ax, ay, sx, sy + r * 0.25f, dx, dy, cut);
    line(sx, sy, bx, by, r * 0.25f, cut);
    circle(bx, by, r * 0.34f, cut);
    circle(bx, by, r * 0.20f, color::text);
    circle(sx, sy, r * 0.25f, cut);
    circle(sx, sy, r * 0.14f, color::text);
}

void game(float cx, float cy, float size) {
    float w = size, h = size * 0.56f;
    u32 dark = mix(color::bg, color::sky_mid, 0.5f);
    rrect(cx - w / 2, cy - h / 2, w, h, size * 0.16f, color::silver);
    circle(cx - w * 0.32f, cy + h * 0.25f, size * 0.14f, color::silver);
    circle(cx + w * 0.32f, cy + h * 0.25f, size * 0.14f, color::silver);
    // a metallic shine along the top
    rrect(cx - w * 0.36f, cy - h * 0.42f, w * 0.72f, h * 0.16f, h * 0.08f,
          with_alpha(color::white, 0x70));
    plus(cx - w * 0.26f, cy - h * 0.02f, size * 0.22f, size * 0.07f, dark);
    // face buttons in the rainbow
    const float buttons[4][2] = {
        {0.20f, -0.02f}, {0.32f, -0.22f}, {0.44f, -0.02f}, {0.32f, 0.18f}};
    for (int i = 0; i < 4; i++) {
        circle(cx + w * buttons[i][0], cy + h * buttons[i][1], size * 0.055f,
               rainbow((float)i / 4.0f));
    }
}

void mouse(float cx, float cy, float size) {
    float w = size * 0.5f, h = size * 0.78f;
    float x = cx - w / 2, y = cy - h / 2;
    rrect(x, y, w, h, w / 2, color::silver);
    // a metallic shine down the left side
    rrect(x + w * 0.14f, y + h * 0.14f, w * 0.16f, h * 0.48f, w * 0.08f,
          with_alpha(color::white, 0x70));
    // the split between the buttons, and the scroll wheel in the rainbow
    float t = std::max(1.0f, size * 0.03f);
    rect(cx - t / 2, y, t, h * 0.36f, color::bg);
    rect(x, y + h * 0.36f, w, t, color::bg);
    float ww = w * 0.2f, wh = h * 0.2f;
    rrect(cx - ww / 2, y + h * 0.1f, ww, wh, ww / 2,
          rainbow((float)(frame_time() % 3000) / 3000.0f));
}

void gear(float cx, float cy, float r, u32 color, u32 bg) {
    bg = solid(bg);
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
    bg = solid(bg);
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
    bg = solid(bg);
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

void spinner(float cx, float cy, float r, u64 time_ms) {
    // one turn per 1.5 s
    wheel(cx, cy, r * 1.15f,
          2.0f * (float)M_PI * (float)(time_ms % 1500) / 1500.0f);
}

} // namespace icon

float hint(float x, float y, const char *button, const std::string &label,
           float scale, u32 label_color) {
    float lh = line_height(scale);
    float r = lh * 0.55f;
    float glyph_scale = scale * 0.85f;
    // a silver button, round for one letter and a pill for START/SELECT,
    // with a shine on its top half
    float gw = std::max(2 * r, text_width(button, glyph_scale) + 8.0f);
    float cy = y + lh / 2.0f;
    rrect(x, cy - r, gw, 2 * r, r, color::silver);
    rrect(x + 1, cy - r + 1, gw - 2, r - 1, r - 1,
          with_alpha(color::white, 0x60));
    text(button, x + gw / 2.0f, cy - line_height(glyph_scale) / 2.0f,
         glyph_scale, color::bg, C2D_AlignCenter);
    float lx = x + gw + 4.0f;
    text(label, lx, y, scale, label_color);
    return lx + text_width(label, scale) + 14.0f;
}

} // namespace ui
