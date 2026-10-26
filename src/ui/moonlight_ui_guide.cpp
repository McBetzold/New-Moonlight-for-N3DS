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

// In-app guides: a paged reader with the current page on the top screen and
// a table of contents plus Previous/Next buttons on the touch screen.

#include "moonlight_ui.hpp"

#include <algorithm>

namespace ui {

// ---- 3D setup guide --------------------------------------------------------

static const GuidePage STEREO_GUIDE[] = {
    {"How 3D streaming works",
     "The 3DS shows true 3D when the PC sends a half side-by-side picture: "
     "two views of the scene packed into one frame, the left eye's view "
     "squeezed into the left half and the right eye's into the right half. "
     "Moonlight hands each half to its eye.\n"
     "Apollo needs nothing special. It streams whatever is on its virtual "
     "display, so the whole job is getting that display to show "
     "side-by-side 3D.\n"
     "Quickest first test: play a side-by-side 3D video full screen on the "
     "PC."},
    {"Set up the 3DS",
     "* In Settings, set 3D mode to Side-by-side.\n"
     "* Resolution: 800x480 (PC games, experimental) for games, so they render "
     "in HD, or "
     "800x480 (Desktop) for videos and the desktop.\n"
     "* Start the stream, then raise the 3D slider. Lowering it all the way "
     "returns to 2D.\n"
     "* If the picture looks blocky, raise the bitrate to 5 to 8 Mbps.\n"
     "* If near things look far away, turn on Swap eyes."},
    {"Games: ReShade",
     "Most PC games have no 3D mode of their own. ReShade with the "
     "SuperDepth3D shader adds one, built from the game's depth buffer.\n"
     "* Run the installer from reshade.me, pick the game and its graphics "
     "API, and include the Depth3D effects, which contain SuperDepth3D.\n"
     "* In the game, press Home to open ReShade, enable SuperDepth3D and set "
     "its stereoscopic mode to Side by Side.\n"
     "* If the depth looks flat or wrong, tune its depth map and divergence "
     "settings.\n"
     "* Skip it in online games with anti-cheat, which may ban ReShade."},
    {"Games: true 3D",
     "Depth-based 3D is an estimate. Rendering a real second camera looks "
     "cleaner around edges, particles and transparent effects.\n"
     "* geo-11 gives many DirectX 11 games proper two-camera 3D and can "
     "output side-by-side. Look up whether your game has a geo-11 fix.\n"
     "* Some games have a stereoscopic option of their own. Choose "
     "side-by-side at half width if it is offered."},
    {"Emulators",
     "Emulators render each eye properly, so they make some of the best 3D.\n"
     "* Dolphin (GameCube, Wii): Graphics, Enhancements tab, Stereoscopic 3D "
     "Mode: Side-by-Side.\n"
     "* Azahar (3DS): Stereoscopic 3D Mode: Side by Side, with a "
     "single-screen layout. A 3DS game, streamed back to a 3DS, in 3D.\n"
     "* RPCS3 (PS3): turn on its side-by-side 3D output in the GPU settings, "
     "for games that supported 3D TVs.\n"
     "Run the emulator full screen on Apollo's display."},
    {"Videos",
     "3D films and clips work when they are half side-by-side, often "
     "labelled HSBS, Half-SBS or just SBS.\n"
     "* Play them full screen on Apollo's display in any player, such as VLC "
     "or a web browser.\n"
     "* Full side-by-side files (Full-SBS, FSBS) are twice as wide and come "
     "out flattened with black bars.\n"
     "* Top-and-bottom files (TAB, OU) can't be shown."},
    {"Check the picture",
     "Before raising the slider, look at the stream in 2D.\n"
     "* Two nearly identical copies side by side, each squeezed to half "
     "width: ready for 3D.\n"
     "* Two copies with normal proportions: full-width side-by-side. Switch "
     "the source to half width.\n"
     "* Copies stacked top and bottom: a format Moonlight can't use.\n"
     "* Bits of one view showing up in the other eye: the 3D picture isn't "
     "filling the screen. Make it full screen on Apollo's display.\n"
     "* Near things look far away: turn on Swap eyes."},
};

void MoonlightUi::show_stereo_guide() {
    show_guide("3D setup guide", STEREO_GUIDE,
               sizeof(STEREO_GUIDE) / sizeof(STEREO_GUIDE[0]));
}

// ---- Wake-on-LAN setup guide -------------------------------------------------

static const GuidePage WOL_GUIDE[] = {
    {"What Wake-on-LAN does",
     "Wake PC (experimental) sends a \"magic packet\" over your network that "
     "tells a sleeping PC to turn on, so you can start streaming without "
     "getting up. It is untested in this version.\n"
     "The PC has to be prepared for it once: in its BIOS, in Windows' "
     "network adapter settings and in Windows' power options. The next "
     "pages walk through each.\n"
     "Moonlight learns the PC's network (MAC) address the first time it "
     "connects while paired, so connect once with the PC on."},
    {"1. Use a wired connection",
     "* Connect the PC to the router with an Ethernet cable. Waking over "
     "Wi-Fi is rarely supported.\n"
     "* Keep the 3DS on the same network as the PC; magic packets do not "
     "cross into other networks or the internet."},
    {"2. Turn it on in the BIOS",
     "Restart the PC and enter its BIOS/UEFI setup, usually with Delete, F2 "
     "or F10 at power-on. The names vary by maker; look under Power, APM or "
     "Advanced:\n"
     "* Enable \"Wake on LAN\", \"Power On By PCI-E\" or \"Resume by PCI-E "
     "Device\".\n"
     "* Disable \"ErP\", \"EuP\" or \"Deep Sleep\", which cut power to the "
     "network card when the PC is off.\n"
     "Save and exit."},
    {"3. Set up the network adapter",
     "In Windows, open Device Manager, expand Network adapters and open "
     "your Ethernet adapter's Properties.\n"
     "* Power Management tab: tick \"Allow this device to wake the "
     "computer\" and \"Only allow a magic packet to wake the computer\".\n"
     "* Advanced tab: set \"Wake on Magic Packet\" to Enabled. If there is a "
     "\"Shutdown Wake-On-Lan\" or similar entry, enable it too."},
    {"4. Windows power options",
     "* Waking from Sleep is the most reliable. Put the PC to sleep instead "
     "of shutting it down.\n"
     "* To wake from Shut down as well, turn off Fast Startup: Control "
     "Panel, Power Options, \"Choose what the power buttons do\", "
     "\"Change settings that are currently unavailable\", then untick "
     "\"Turn on fast startup\".\n"
     "* Hibernate support depends on the PC."},
    {"5. Try it",
     "* Put the PC to sleep and press Y on the Computers screen until it "
     "shows Offline.\n"
     "* Press X on the PC and choose Wake PC (experimental). Moonlight "
     "resends the packet every 10 seconds and connects as soon as the PC "
     "answers, for up to 90 seconds.\n"
     "* If it never wakes, check the BIOS and adapter settings again; some "
     "PCs only wake from Sleep."},
};

void MoonlightUi::show_wol_guide() {
    show_guide("Wake-on-LAN setup", WOL_GUIDE,
               sizeof(WOL_GUIDE) / sizeof(WOL_GUIDE[0]));
}

// ---- Guide reader ----------------------------------------------------------

static const float TOOLBAR_H = 36.0f;
static const float HINTS_Y = 220.0f; // top of the hints bar (draw_hints)

// Top screen card
static const float CARD_X = 16.0f;
static const float CARD_Y = 38.0f;
static const float CARD_W = TOP_W - 32.0f;
static const float CARD_BOTTOM = HINTS_Y - 8.0f;
static const float TEXT_X = CARD_X + 14.0f;
static const float TEXT_W = CARD_W - 28.0f;
static const float TITLE_SCALE = 0.55f;
static const float BODY_SCALE = 0.42f;
static const float BULLET_INDENT = 12.0f;
static const float PARAGRAPH_GAP = 5.0f;

// Touch screen layout
static const float TOC_X = 8.0f;
static const float TOC_W = BOT_W - 16.0f;
static const float TOC_Y = TOOLBAR_H + 6.0f;
static const float TOC_ROW_H = 20.0f;
static const float TOC_GAP = 2.0f;
static const float NAV_H = 32.0f;
static const float NAV_Y = BOT_H - NAV_H - 6.0f;
static const float NAV_GAP = 8.0f;
static const float NAV_W = (BOT_W - 16.0f - NAV_GAP) / 2.0f;

struct GuideLine {
    std::string text;
    float indent = 0.0f;
    bool bullet = false; // first line of a bullet point
    float gap = 0.0f;    // extra space above (new paragraph)
};

static std::vector<GuideLine> layout_body(const char *body) {
    std::vector<GuideLine> out;
    std::string s(body ? body : "");
    size_t start = 0;
    bool first = true;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        size_t end = nl == std::string::npos ? s.size() : nl;
        std::string para = s.substr(start, end - start);
        start = end + 1;
        if (para.empty()) {
            continue;
        }
        bool bullet = para.compare(0, 2, "* ") == 0;
        if (bullet) {
            para = para.substr(2);
        }
        float indent = bullet ? BULLET_INDENT : 0.0f;
        auto lines = wrap(para, BODY_SCALE, TEXT_W - indent);
        for (size_t i = 0; i < lines.size(); i++) {
            GuideLine l;
            l.text = lines[i];
            l.indent = indent;
            l.bullet = bullet && i == 0;
            l.gap = (i == 0 && !first) ? PARAGRAPH_GAP : 0.0f;
            out.push_back(l);
        }
        first = false;
    }
    return out;
}

static float body_top() {
    return CARD_Y + 10.0f + line_height(TITLE_SCALE) + 6.0f;
}

// Largest first line that still fills the card, so scrolling stops once the
// last line is visible.
static int max_first_line(const std::vector<GuideLine> &lines) {
    float avail = CARD_BOTTOM - 6.0f - body_top();
    float used = 0.0f;
    int first = (int)lines.size();
    while (first > 0) {
        float h = line_height(BODY_SCALE) + lines[first - 1].gap;
        if (used + h > avail) {
            break;
        }
        used += h;
        first--;
    }
    return first;
}

static void toc_row_rect(int index, float &x, float &y, float &w, float &h) {
    x = TOC_X;
    y = TOC_Y + index * (TOC_ROW_H + TOC_GAP);
    w = TOC_W;
    h = TOC_ROW_H;
}

void MoonlightUi::show_guide(const std::string &title, const GuidePage *pages,
                             int count) {
    if (count <= 0) {
        return;
    }
    int page = 0;
    int first = 0;
    std::vector<GuideLine> lines = layout_body(pages[0].body);
    auto go_to = [&](int p) {
        p = std::max(0, std::min(count - 1, p));
        if (p != page) {
            page = p;
            first = 0;
            lines = layout_body(pages[page].body);
        }
    };

    input_reset();
    while (true) {
        if (!poll()) {
            return;
        }
        int max_first = max_first_line(lines);

        // ---- input
        if (toolbar_hit(in_, 1) == 0 || in_.pressed(KEY_B)) {
            break;
        }
        if ((in_.nav & KEY_DLEFT) || in_.pressed(KEY_L)) {
            go_to(page - 1);
        }
        if ((in_.nav & KEY_DRIGHT) || in_.pressed(KEY_R)) {
            go_to(page + 1);
        }
        if (in_.pressed(KEY_A)) {
            if (page == count - 1) {
                break;
            }
            go_to(page + 1);
        }
        if (in_.nav & KEY_DUP) {
            first = std::max(0, first - 1);
        }
        if (in_.nav & KEY_DDOWN) {
            first = std::min(max_first, first + 1);
        }
        if (in_.tap) {
            for (int i = 0; i < count; i++) {
                float x, y, w, h;
                toc_row_rect(i, x, y, w, h);
                if (in_.tapped_in(x, y, w, h)) {
                    go_to(i);
                }
            }
            if (in_.tapped_in(TOC_X, NAV_Y, NAV_W, NAV_H)) {
                go_to(page - 1);
            } else if (in_.tapped_in(TOC_X + NAV_W + NAV_GAP, NAV_Y, NAV_W,
                                     NAV_H)) {
                if (page == count - 1) {
                    break;
                }
                go_to(page + 1);
            }
        }
        max_first = max_first_line(lines);
        first = std::max(0, std::min(first, max_first));

        if (!gfx_ready()) {
            continue;
        }
        frame_begin();

        // ---- top screen: the page
        scene(Screen::Top, color::bg);
        draw_header(title,
                    "Page " + std::to_string(page + 1) + " of " +
                        std::to_string(count),
                    true);
        rrect(CARD_X, CARD_Y, CARD_W, CARD_BOTTOM - CARD_Y, 8, color::surface);
        rect(CARD_X + 8, CARD_Y, CARD_W - 16, 3, color::accent);
        text(ellipsize(pages[page].title, TITLE_SCALE, TEXT_W), TEXT_X,
             CARD_Y + 10.0f, TITLE_SCALE, color::accent_text);

        const float lh = line_height(BODY_SCALE);
        const float bottom = CARD_BOTTOM - 6.0f;
        float y = body_top();
        int last_drawn = first - 1;
        for (int i = first; i < (int)lines.size(); i++) {
            const GuideLine &l = lines[i];
            float gap = i == first ? 0.0f : l.gap;
            if (y + gap + lh > bottom) {
                break;
            }
            y += gap;
            if (l.bullet) {
                circle(TEXT_X + 4.0f, y + lh / 2.0f, 2.0f, color::accent);
            }
            text(l.text, TEXT_X + l.indent, y, BODY_SCALE, color::text_dim);
            y += lh;
            last_drawn = i;
        }
        // Scroll indicators at the card's right edge
        float ax = CARD_X + CARD_W - 12.0f;
        if (first > 0) {
            float ay = body_top() + 2.0f;
            triangle(ax, ay, ax - 5.0f, ay + 6.0f, ax + 5.0f, ay + 6.0f,
                     color::accent_text);
        }
        bool more = last_drawn < (int)lines.size() - 1;
        if (more) {
            float ay = bottom - 2.0f;
            triangle(ax, ay, ax - 5.0f, ay - 6.0f, ax + 5.0f, ay - 6.0f,
                     color::accent_text);
        }

        std::vector<std::pair<const char *, std::string>> hints;
        hints.push_back({"<>", "Page"});
        if (first > 0 || more) {
            hints.push_back({"^v", "Scroll"});
        }
        hints.push_back({"A", page == count - 1 ? "Done" : "Next"});
        hints.push_back({"B", "Close"});
        draw_hints(hints);

        // ---- touch screen: contents and navigation
        scene(Screen::Bottom, color::bg);
        for (int i = 0; i < count; i++) {
            float x, ry, w, h;
            toc_row_rect(i, x, ry, w, h);
            bool sel = i == page;
            u32 fill = sel ? color::card_hi : color::card;
            if (in_.touching_in(x, ry, w, h)) {
                fill = mix(fill, color::white, 0.15f);
            }
            rrect(x, ry, w, h, 4, fill);
            if (sel) {
                rect(x, ry + 3, 3, h - 6, color::accent);
            }
            float ts = 0.4f;
            float ty = ry + (h - line_height(ts)) / 2.0f;
            text(std::to_string(i + 1), x + 16.0f, ty, ts,
                 sel ? color::accent_text : color::text_faint,
                 C2D_AlignCenter);
            text(ellipsize(pages[i].title, ts, w - 40.0f), x + 30.0f, ty, ts,
                 sel ? color::text : color::text_dim);
        }

        auto nav_button = [&](float x, const std::string &label, bool enabled,
                              bool primary) {
            u32 fill = primary ? color::accent : color::card_hi;
            if (!enabled) {
                fill = color::card;
            } else if (in_.touching_in(x, NAV_Y, NAV_W, NAV_H)) {
                fill = mix(fill, color::white, 0.2f);
            }
            rrect(x, NAV_Y, NAV_W, NAV_H, 6, fill);
            text(label, x + NAV_W / 2.0f,
                 NAV_Y + (NAV_H - line_height(0.46f)) / 2.0f, 0.46f,
                 enabled ? color::text : color::text_faint, C2D_AlignCenter);
        };
        nav_button(TOC_X, "Previous", page > 0, false);
        nav_button(TOC_X + NAV_W + NAV_GAP,
                   page == count - 1 ? "Done" : "Next", true, true);

        draw_toolbar({{ToolButton::Icon::Back, "Close", true}});

        frame_end();
    }
    input_reset();
}

} // namespace ui
