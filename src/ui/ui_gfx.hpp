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

// Thin drawing layer over citro2d used by the launcher UI (host list, app
// grid, settings). The video renderers drive the GPU directly, so this layer
// is torn down with gfx_fini() right before a stream starts and brought back
// with gfx_init() once it ends.
#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <string>
#include <vector>

namespace ui {

constexpr float TOP_W = 400.0f;
constexpr float TOP_H = 240.0f;
constexpr float BOT_W = 320.0f;
constexpr float BOT_H = 240.0f;

// Height of a line of system-font text at scale 1.0
constexpr float FONT_LINE = 30.0f;

enum class Screen { Top, Bottom };

constexpr u32 rgba(u8 r, u8 g, u8 b, u8 a = 255) {
    return C2D_Color32(r, g, b, a);
}
u32 with_alpha(u32 color, u8 alpha);
u32 mix(u32 a, u32 b, float t);

// The New Moonlight look, from the app icon and HOME Menu banner: a night
// sky with twinkling stars, Isaac's rainbow wheel and silver letters with
// rainbow edges (and a red 3)
namespace color {
constexpr u32 bg = rgba(0x07, 0x0b, 0x1f);      // the sky at the edges
constexpr u32 sky_mid = rgba(0x1b, 0x14, 0x46); // the sky in the middle
constexpr u32 glass = rgba(0x10, 0x0e, 0x30, 0xc8); // panels over the sky
constexpr u32 surface = rgba(0x1a, 0x17, 0x40);     // dialogs
constexpr u32 card = rgba(0x26, 0x22, 0x56);
constexpr u32 card_hi = rgba(0x38, 0x32, 0x74);
constexpr u32 header = rgba(0x04, 0x06, 0x16, 0xd8);
constexpr u32 border = rgba(0x40, 0x3a, 0x80);
constexpr u32 text = rgba(0xf4, 0xf4, 0xfa);
constexpr u32 silver = rgba(0xd4, 0xd8, 0xe6);
constexpr u32 text_dim = rgba(0xb8, 0xb6, 0xd4);
constexpr u32 text_faint = rgba(0x7c, 0x7a, 0xa4);
constexpr u32 accent = rgba(0x80, 0x60, 0xf0);
constexpr u32 accent_dim = rgba(0x48, 0x34, 0xa0);
constexpr u32 accent_text = rgba(0xc8, 0xba, 0xff);
constexpr u32 red = rgba(0xe2, 0x34, 0x34); // the logo's red M and 3
constexpr u32 ok = rgba(0x5a, 0xbe, 0x50);
constexpr u32 warn = rgba(0xf4, 0x8c, 0x28);
constexpr u32 err = red;
constexpr u32 white = rgba(0xff, 0xff, 0xff);
constexpr u32 black = rgba(0, 0, 0);
constexpr u32 overlay = rgba(0, 0, 0, 0xa0);
constexpr u32 skyblue = rgba(0x7f, 0xd3, 0xd8); // the rainbow's teal
} // namespace color

bool gfx_init();
void gfx_fini();
bool gfx_ready();

void frame_begin();
void scene(Screen screen, u32 clear_color);
void frame_end();

// ---- Text -----------------------------------------------------------------
float text_width(const std::string &str, float scale);
inline float line_height(float scale) { return FONT_LINE * scale; }
// Draws a single line. For C2D_AlignCenter x is the center, for
// C2D_AlignRight x is the right edge.
void text(const std::string &str, float x, float y, float scale, u32 color,
          u32 align = C2D_AlignLeft);
std::string ellipsize(const std::string &str, float scale, float max_width);
std::vector<std::string> wrap(const std::string &str, float scale,
                              float max_width);
// Draws wrapped lines; returns the total height used.
float text_block(const std::vector<std::string> &lines, float x, float y,
                 float scale, u32 color, u32 align = C2D_AlignLeft,
                 float line_gap = 0.0f);

// ---- Shapes ---------------------------------------------------------------
void rect(float x, float y, float w, float h, u32 color);
void rect_gradient_v(float x, float y, float w, float h, u32 top, u32 bottom);
void rect_outline(float x, float y, float w, float h, float thickness,
                  u32 color);
void rrect(float x, float y, float w, float h, float radius, u32 color);
void rrect_outline(float x, float y, float w, float h, float radius,
                   float thickness, u32 color);
void circle(float cx, float cy, float radius, u32 color);
void ring(float cx, float cy, float radius, float thickness, u32 color,
          u32 hole_color);
void line(float x0, float y0, float x1, float y1, float thickness, u32 color);
void triangle(float x0, float y0, float x1, float y1, float x2, float y2,
              u32 color);
// Rounded pill with centered label; returns the pill width.
float pill(float x, float y, const std::string &label, float scale, u32 fill,
           u32 text_color, float pad_x = 8.0f);

// ---- The New Moonlight look -------------------------------------------------
// osGetTime() at frame_begin(), for animations
u64 frame_time();
// The wheel's rainbow; t wraps around it once per 1.0
u32 rainbow(float t);
// Night sky with nebula wisps and twinkling stars; drawn first on a screen
void sky(Screen screen);
// Translucent panel over the sky with a highlight along its top edge
void panel(float x, float y, float w, float h, float radius);
// Horizontal rainbow strip whose colours drift slowly
void rainbow_bar(float x, float y, float w, float h);
// Rainbow outline of a rounded rectangle (selection frames); the colours
// travel slowly around it
void rainbow_frame(float x, float y, float w, float h, float radius,
                   float thickness);
// Isaac's wheel: rainbow ring and spokes around a silver hub, turned by
// `angle` (radians)
void wheel(float cx, float cy, float r, float angle);
// The wheel turning once per 20 s, like on the HOME Menu banner
void logo_wheel(float cx, float cy, float r);
// Silver text with a dark edge
void silver_text(const std::string &str, float x, float y, float scale,
                 u32 align = C2D_AlignLeft);
// The logo's lettering: silver letters with a dark edge, each outlined in
// the next colour of the rainbow; the letter at red_index is red. Returns
// the width.
float logo_text(const std::string &str, float x, float y, float scale,
                int red_index = -1);
float logo_text_width(const std::string &str, float scale);

// ---- Icons (procedural, no assets required) --------------------------------
namespace icon {
void moon(float cx, float cy, float r, u32 color, u32 bg);
void desktop(float cx, float cy, float size, u32 color);
void gear(float cx, float cy, float r, u32 color, u32 bg);
void plus(float cx, float cy, float size, float thickness, u32 color);
void lock(float cx, float cy, float size, u32 color, u32 bg);
void warning(float cx, float cy, float size, u32 color, u32 mark);
void play(float cx, float cy, float size, u32 color);
void stop(float cx, float cy, float size, u32 color);
void refresh(float cx, float cy, float r, u32 color, u32 bg);
void back_arrow(float cx, float cy, float size, u32 color);
void check(float cx, float cy, float size, u32 color);
void cross(float cx, float cy, float size, u32 color);
void dots(float cx, float cy, float size, u32 color);
void gamepad(float cx, float cy, float size, u32 color, u32 detail);
void wifi_off(float cx, float cy, float size, u32 color);
// The wheel spinning quickly
void spinner(float cx, float cy, float r, u64 time_ms);
} // namespace icon

// Draws a button glyph (circle with a letter) followed by a label. Returns the
// x coordinate after the label so hints can be chained.
float hint(float x, float y, const char *button, const std::string &label,
           float scale = 0.42f, u32 label_color = color::text_dim);

} // namespace ui
