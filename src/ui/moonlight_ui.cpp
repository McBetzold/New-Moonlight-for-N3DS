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

#include "moonlight_ui.hpp"

#include "../system/pair_record.hpp"
#include "../system/wake_on_lan.hpp"
#include "discovery.hpp"

#include <errors.h>
#include <http.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace ui {

// Layout of the bottom-screen grids
static const float TOOLBAR_H = 36.0f;
static const float PC_X0 = 8.0f, PC_Y0 = 44.0f, PC_W = 148.0f, PC_H = 88.0f,
                   PC_GAP = 8.0f;
static const int PC_COLS = 2;
static const float APP_X0 = 6.0f, APP_Y0 = 42.0f, APP_W = 74.0f,
                   APP_H = 108.0f, APP_GAP_X = 4.0f, APP_GAP_Y = 8.0f;
static const int APP_COLS = 4;
static const float ART_SCALE = 64.0f / 96.0f; // 64x85 tiles

static const float HEADER_H = 30.0f;
static const float HINTS_Y = 220.0f;

// Apollo's virtual display entry and Sunshine's fallback desktop (fixed IDs)
static const char *const APOLLO_VIRTUAL_DISPLAY_UUID =
    "8902CB19-674A-403D-A587-41B092E900BA";
static const char *const FALLBACK_DESKTOP_UUID =
    "EAAC6159-089A-46A9-9E24-6436885F6610";

static AppEntry::Builtin builtin_kind(const std::string &name,
                                      const std::string &uuid) {
    if (strcasecmp(uuid.c_str(), APOLLO_REMOTE_INPUT_UUID) == 0) {
        return AppEntry::Builtin::RemoteInput;
    }
    if (strcasecmp(uuid.c_str(), APOLLO_VIRTUAL_DISPLAY_UUID) == 0) {
        return AppEntry::Builtin::VirtualDisplay;
    }
    if (strcasecmp(uuid.c_str(), FALLBACK_DESKTOP_UUID) == 0 ||
        strcasecmp(name.c_str(), "Desktop") == 0) {
        return AppEntry::Builtin::Desktop;
    }
    return AppEntry::Builtin::None;
}

// Box art for the built-in entries in the launcher's look: a small night
// sky with the entry's icon
static void builtin_art(AppEntry::Builtin kind, float x, float y, float w,
                        float h) {
    float mid = h * 0.6f;
    u32 deep = mix(color::sky_mid, color::bg, 0.5f);
    rect_gradient_v(x, y, w, mid, color::sky_mid, deep);
    rect_gradient_v(x, y + mid, w, h - mid, deep, color::bg);
    static const float stars[][2] = {{0.14f, 0.1f}, {0.8f, 0.16f},
                                     {0.62f, 0.07f}, {0.24f, 0.8f},
                                     {0.86f, 0.72f}, {0.08f, 0.5f},
                                     {0.58f, 0.9f}};
    for (auto &st : stars) {
        circle(x + w * st[0], y + h * st[1], std::max(0.7f, w * 0.009f),
               with_alpha(color::silver, 0x90));
    }
    rect_outline(x, y, w, h, 1, color::border);
    float cx = x + w / 2.0f, cy = y + h * 0.5f, size = w * 0.62f;
    switch (kind) {
    case AppEntry::Builtin::Desktop:
        icon::desktop(cx, cy, size, color::silver);
        break;
    case AppEntry::Builtin::VirtualDisplay:
        icon::virtual_display(cx, cy, size);
        break;
    case AppEntry::Builtin::RemoteInput:
        icon::mouse(cx, cy, size * 0.9f);
        break;
    case AppEntry::Builtin::None:
        break;
    }
}

static std::string gs_status_text(int status) {
    const char *err = gs_error ? gs_error : "";
    switch (status) {
    case GS_OUT_OF_MEMORY:
        return "Not enough memory";
    case GS_ERROR:
        return std::string("Server error: ") + err;
    case GS_INVALID:
        return std::string("Invalid data received from the PC: ") + err;
    case GS_UNSUPPORTED_VERSION:
        return std::string("Unsupported server version: ") + err;
    case GS_IO_ERROR:
        return err[0] ? std::string("Unable to reach the PC (") + err + ")"
                      : "Unable to reach the PC";
    default:
        return err[0] ? std::string(err) : "Unable to connect to the PC";
    }
}

static std::string describe_server(const SERVER_DATA &server) {
    std::string desc = server.isNvidiaSoftware ? "GeForce Experience"
                       : server.isApollo       ? "Apollo"
                                               : "Sunshine";
    if (server.gpuType && server.gpuType[0]) {
        desc += "  \xC2\xB7  ";
        desc += server.gpuType;
    }
    return desc;
}

static int grid_hit(float tx, float ty, int count, int cols, float x0, float y0,
                    float w, float h, float gap_x, float gap_y, float scroll) {
    if (ty < TOOLBAR_H) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        int row = i / cols, col = i % cols;
        float x = x0 + col * (w + gap_x);
        float y = y0 + row * (h + gap_y) - scroll;
        if (tx >= x && tx < x + w && ty >= y && ty < y + h) {
            return i;
        }
    }
    return -1;
}

static float grid_max_scroll(int count, int cols, float y0, float h,
                             float gap_y) {
    int rows = (count + cols - 1) / cols;
    float content = rows * (h + gap_y) - gap_y;
    float visible = BOT_H - y0 - 4.0f;
    return std::max(0.0f, content - visible);
}

static void scroll_to_row(float &scroll, int row, float y0, float h,
                          float gap_y, float max_scroll) {
    float top = row * (h + gap_y);
    float bottom = top + h;
    float visible = BOT_H - y0 - 4.0f;
    if (top < scroll) {
        scroll = top;
    } else if (bottom > scroll + visible) {
        scroll = bottom - visible;
    }
    scroll = std::max(0.0f, std::min(scroll, max_scroll));
}

// ---------------------------------------------------------------------------

MoonlightUi::MoonlightUi(PCONFIGURATION config) : config_(config) {
    srand((unsigned)osGetTime());
    for (auto &rec : hosts::load()) {
        auto h = std::make_shared<HostEntry>();
        h->address = rec.address;
        h->port = rec.port;
        h->name = rec.name;
        h->custom_name = rec.custom_name;
        h->mac = rec.mac;
        hosts_.push_back(h);
    }
}

MoonlightUi::~MoonlightUi() {
    Worker::instance().drop_background();
    // The app is closing: a probe, app list or box art request in progress
    // would otherwise hold the close for up to its 60 s timeout
    http_abort_all();
    Worker::instance().shutdown();
    free_boxart();
    gfx_fini();
}

void MoonlightUi::run() {
    if (!gfx_init()) {
        printf("Failed to initialize the UI renderer\n");
        return;
    }
    refresh_all_hosts(false);
    maybe_resume_on_start();

    while (!quit_) {
        if (!poll()) {
            break;
        }
        switch (view_) {
        case View::Pcs:
            pcs_update(in_);
            break;
        case View::Apps:
            apps_update(in_);
            break;
        case View::Settings:
            settings_update(in_);
            break;
        }
        if (quit_) {
            break;
        }
        render(nullptr);
    }
    // Settings are saved when the page is left; closing the app from the
    // HOME Menu while it is open would otherwise lose the changes
    if (view_ == View::Settings) {
        config_save((char *)MOONLIGHT_3DS_PATH "/moonlight.conf", config_);
    }
}

// ---- frame plumbing -------------------------------------------------------

bool MoonlightUi::poll() {
    if (!aptMainLoop()) {
        quit_ = true;
        // Any job a dialog is waiting on fails within a second instead of
        // running to its timeout (up to a minute) while the close waits
        http_abort_all();
        return false;
    }
    input_poll(in_);
    now_ = in_.now;
    pump();
    return !quit_;
}

void MoonlightUi::pump() {
    for (auto it = probes_.begin(); it != probes_.end();) {
        if (it->job->is_done()) {
            // Skip results for a PC that was deleted while it was checked. (A
            // PC being added is probed before it joins hosts_, so this must
            // be a flag rather than a hosts_ lookup.)
            if (!it->host->deleted) {
                apply_probe(*it);
            } else {
                it->host->probing = false;
            }
            it = probes_.erase(it);
        } else {
            ++it;
        }
    }

    if (pending_apps_ && pending_apps_->job->is_done()) {
        auto pending = std::move(pending_apps_);
        if (pending->result->status == GS_OK && pending->host == cur_host_) {
            apply_apps(pending->host, *pending->result);
        }
    }

    if (gfx_ready()) {
        for (auto &app : apps_) {
            if (app.art_state == AppEntry::ArtState::Loading && app.job &&
                app.job->is_done()) {
                if (app.art_result && app.art_result->ok &&
                    boxart::texture_upload(app.art, app.art_result->tiled)) {
                    app.art_state = AppEntry::ArtState::Ready;
                } else {
                    app.art_state = AppEntry::ArtState::Missing;
                }
                app.job = nullptr;
                app.art_result = nullptr;
            }
        }
    }
}

void MoonlightUi::render(const Modal *modal) {
    if (!gfx_ready()) {
        return;
    }
    frame_begin();

    scene(Screen::Top, color::bg);
    sky(Screen::Top);
    draw_top();
    if (modal) {
        draw_modal_top(*modal);
    }
    draw_toast();

    scene(Screen::Bottom, color::bg);
    sky(Screen::Bottom);
    draw_bottom();
    if (modal) {
        draw_modal_bottom(*modal);
    }

    frame_end();
}

void MoonlightUi::draw_top() {
    switch (view_) {
    case View::Pcs:
        pcs_draw_top();
        break;
    case View::Apps:
        apps_draw_top();
        break;
    case View::Settings:
        settings_draw_top();
        break;
    }
}

void MoonlightUi::draw_bottom() {
    switch (view_) {
    case View::Pcs:
        pcs_draw_bottom();
        break;
    case View::Apps:
        apps_draw_bottom();
        break;
    case View::Settings:
        settings_draw_bottom();
        break;
    }
}

void MoonlightUi::draw_header(const std::string &title,
                              const std::string &subtitle, bool logo) {
    rect(0, 0, TOP_W, HEADER_H, color::header);
    rainbow_bar(0, HEADER_H, TOP_W, 2);
    float x = 12.0f;
    if (logo) {
        logo_wheel(x + 10, HEADER_H / 2, 11);
        x += 28.0f;
        logo_text(title, x - 2.0f, 3.0f, 0.6f);
    } else {
        silver_text(title, x, 5.0f, 0.6f);
    }
    if (!subtitle.empty()) {
        text(subtitle, TOP_W - 12.0f, 9.0f, 0.42f, color::text_dim,
             C2D_AlignRight);
    }
}

void MoonlightUi::draw_hints(
    const std::vector<std::pair<const char *, std::string>> &hints) {
    rect(0, HINTS_Y - 2, TOP_W, TOP_H - HINTS_Y + 2, color::header);
    rect(0, HINTS_Y - 2, TOP_W, 1, color::border);
    float x = 12.0f;
    for (const auto &h : hints) {
        x = hint(x, HINTS_Y + 2, h.first, h.second);
    }
}

void MoonlightUi::draw_toolbar(const std::vector<ToolButton> &buttons) {
    rect(0, 0, BOT_W, TOOLBAR_H, color::header);
    rainbow_bar(0, TOOLBAR_H - 2, BOT_W, 2);
    if (buttons.empty()) {
        return;
    }
    float bw = BOT_W / (float)buttons.size();
    for (size_t i = 0; i < buttons.size(); i++) {
        const auto &b = buttons[i];
        float x = i * bw;
        u32 c = b.enabled ? color::text : color::text_faint;
        bool pressed = b.enabled && in_.touching_in(x, 0, bw, TOOLBAR_H);
        if (pressed) {
            rect(x, 0, bw, TOOLBAR_H - 2, color::card_hi);
        }
        if (i > 0) {
            rect(x, 6, 1, TOOLBAR_H - 12, color::border);
        }
        float cx = x + bw / 2.0f;
        float iy = 13.0f;
        u32 bg = pressed ? color::card_hi : color::header;
        switch (b.icon) {
        case ToolButton::Icon::Add:
            icon::plus(cx, iy, 13, 3, c);
            break;
        case ToolButton::Icon::Refresh:
            icon::refresh(cx, iy, 7, c, bg);
            break;
        case ToolButton::Icon::Options:
            icon::dots(cx, iy, 16, c);
            break;
        case ToolButton::Icon::Settings:
            icon::gear(cx, iy, 8, c, bg);
            break;
        case ToolButton::Icon::Back:
            icon::back_arrow(cx, iy, 15, c);
            break;
        case ToolButton::Icon::Stop:
            icon::stop(cx, iy, 11, c);
            break;
        case ToolButton::Icon::Exit:
            icon::cross(cx, iy, 14, c);
            break;
        }
        text(b.label, cx, 21.0f, 0.36f, c, C2D_AlignCenter);
    }
}

int MoonlightUi::toolbar_hit(const Input &in, int count) const {
    if (!in.tap || count <= 0 || in.ty >= TOOLBAR_H) {
        return -1;
    }
    int idx = (int)(in.tx / (BOT_W / (float)count));
    return std::max(0, std::min(count - 1, idx));
}

void MoonlightUi::toast(const std::string &message) {
    toast_text_ = message;
    toast_until_ = osGetTime() + 2500;
}

void MoonlightUi::draw_toast() {
    if (toast_text_.empty() || now_ > toast_until_) {
        return;
    }
    float w = text_width(toast_text_, 0.45f) + 28.0f;
    float h = line_height(0.45f) + 10.0f;
    float x = (TOP_W - w) / 2.0f;
    float y = HINTS_Y - h - 10.0f;
    rrect(x, y, w, h, h / 2.0f, color::surface);
    rainbow_frame(x, y, w, h, h / 2.0f, 2.0f);
    text(toast_text_, TOP_W / 2.0f, y + 5.0f, 0.45f, color::text,
         C2D_AlignCenter);
}

void MoonlightUi::modal_button_rect(const Modal &m, int index, float &x,
                                    float &y, float &w, float &h) const {
    int n = (int)m.buttons.size();
    bool two_line = !m.subtitles.empty();
    if (n <= 2 && !m.list) {
        float gap = 12.0f;
        w = (BOT_W - 40.0f - (n - 1) * gap) / (float)n;
        h = 44.0f;
        x = 20.0f + index * (w + gap);
        // below the spinner and its text when busy (draw_modal_bottom)
        y = m.busy ? 186.0f : 104.0f;
    } else {
        // Stack vertically and shrink the buttons so the whole list always
        // fits on the 240px screen (six items overflowed at a fixed 36px).
        const float margin = 12.0f;
        const float avail = BOT_H - 2.0f * margin;
        float gap = n > 4 ? 6.0f : 8.0f;
        float preferred = two_line ? 46.0f : 36.0f;
        h = std::min(preferred, (avail - (n - 1) * gap) / (float)n);
        w = two_line ? 288.0f : 232.0f;
        x = (BOT_W - w) / 2.0f;
        float total = n * h + (n - 1) * gap;
        y = margin + (avail - total) / 2.0f + index * (h + gap);
    }
}

void MoonlightUi::draw_modal_top(const Modal &m) {
    rect(0, 0, TOP_W, TOP_H, color::overlay);
    const float x = 28.0f, w = TOP_W - 56.0f;
    float y = 30.0f, h = 180.0f;
    rrect(x, y, w, h, 8, color::surface);
    rainbow_bar(x + 8, y, w - 16, 3);
    float cy = y + 12.0f;
    silver_text(ellipsize(m.title, 0.55f, w - 32.0f), x + 16.0f, cy, 0.55f);
    cy += line_height(0.55f) + 6.0f;
    if (!m.message.empty()) {
        auto lines = wrap(m.message, 0.42f, w - 32.0f);
        if (lines.size() > 5) {
            lines.resize(5);
        }
        cy += text_block(lines, x + 16.0f, cy, 0.42f, color::text_dim) + 4.0f;
    }
    if (!m.big_text.empty()) {
        float scale = 1.3f;
        float tw = logo_text_width(m.big_text, scale);
        float bw = tw + 32.0f;
        float bh = line_height(scale) + 12.0f;
        float by = std::min(cy, y + h - bh - 12.0f);
        rrect((TOP_W - bw) / 2.0f, by, bw, bh, 8, color::card);
        logo_text(m.big_text, (TOP_W - tw) / 2.0f, by + 4.0f, scale);
    }
}

void MoonlightUi::draw_modal_bottom(const Modal &m) {
    rect(0, 0, BOT_W, BOT_H, color::overlay);
    if (m.busy) {
        icon::spinner(BOT_W / 2.0f, BOT_H / 2.0f, 18.0f, now_);
        if (!m.busy_text.empty()) {
            auto lines = wrap(m.busy_text, 0.42f, BOT_W - 48.0f);
            if (lines.size() > 2) {
                lines.resize(2);
            }
            text_block(lines, BOT_W / 2.0f, BOT_H / 2.0f + 30.0f, 0.42f,
                       color::text_dim, C2D_AlignCenter);
        }
    }
    for (size_t i = 0; i < m.buttons.size(); i++) {
        float x, y, w, h;
        modal_button_rect(m, (int)i, x, y, w, h);
        bool sel = (int)i == m.selected;
        bool pressed = in_.touching_in(x, y, w, h);
        u32 fill = color::card_hi;
        if ((int)i == m.danger_index) {
            fill = sel ? color::err : mix(color::card_hi, color::err, 0.35f);
        } else if (sel) {
            fill = color::accent;
        }
        if (pressed) {
            fill = mix(fill, color::white, 0.2f);
        }
        rrect(x, y, w, h, 6, fill);
        if (sel) {
            rainbow_frame(x - 3, y - 3, w + 6, h + 6, 9, 2.5f);
        }

        std::string subtitle =
            i < m.subtitles.size() ? m.subtitles[i] : std::string();
        std::string badge = i < m.badges.size() ? m.badges[i] : std::string();
        float badge_w = 0.0f;
        if (!badge.empty()) {
            float bs = 0.32f;
            badge_w = text_width(badge, bs) + 14.0f;
            float bh = line_height(bs) + 4.0f;
            u32 badge_fill = sel ? color::accent_dim : color::card;
            u32 badge_text = sel ? color::text : color::text_dim;
            pill(x + w - badge_w - 8.0f, y + (h - bh) / 2.0f, badge, bs,
                 badge_fill, badge_text, 7.0f);
            badge_w += 12.0f;
        }
        if (subtitle.empty()) {
            text(ellipsize(m.buttons[i], 0.48f, w - 16.0f - badge_w),
                 x + (w - badge_w) / 2.0f, y + (h - line_height(0.48f)) / 2.0f,
                 0.48f, color::text, C2D_AlignCenter);
        } else {
            // Two-line entry: name on top, detail (e.g. the IP) underneath.
            float ts = 0.46f, ss = 0.34f;
            float block = line_height(ts) + line_height(ss) - 4.0f;
            float ty = y + (h - block) / 2.0f;
            float avail_w = w - 24.0f - badge_w;
            text(ellipsize(m.buttons[i], ts, avail_w), x + 12.0f, ty, ts,
                 color::text);
            text(ellipsize(subtitle, ss, avail_w), x + 12.0f,
                 ty + line_height(ts) - 4.0f, ss,
                 sel ? color::accent_text : color::text_dim);
        }
    }
    if (m.buttons.empty() && !m.busy) {
        text("Press A to continue", BOT_W / 2.0f, 200.0f, 0.4f,
             color::text_faint, C2D_AlignCenter);
    }
}

// ---- modals ---------------------------------------------------------------

int MoonlightUi::show_modal(Modal &m, const std::function<bool()> &finished) {
    int n = (int)m.buttons.size();
    m.selected = std::max(0, std::min(n - 1, m.selected));
    bool vertical = n > 2 || m.list;
    input_reset();
    while (true) {
        if (!poll()) {
            return -3;
        }
        if (finished && finished()) {
            return -2;
        }
        if (n > 0) {
            u32 prev_key = vertical ? KEY_DUP : KEY_DLEFT;
            u32 next_key = vertical ? KEY_DDOWN : KEY_DRIGHT;
            if ((in_.nav & prev_key) && m.selected > 0) {
                m.selected--;
            }
            if ((in_.nav & next_key) && m.selected < n - 1) {
                m.selected++;
            }
            if (in_.pressed(KEY_A)) {
                return m.selected;
            }
            if (in_.tap) {
                for (int i = 0; i < n; i++) {
                    float x, y, w, h;
                    modal_button_rect(m, i, x, y, w, h);
                    if (in_.tapped_in(x, y, w, h)) {
                        return i;
                    }
                }
            }
            // B cancels any modal that offers buttons, including busy ones
            // with a Cancel/Skip/Stay button (wake, reconnect, resume)
            if (in_.pressed(KEY_B)) {
                return -1;
            }
        } else if (!m.busy) {
            if (in_.pressed(KEY_A) || in_.pressed(KEY_B) || in_.tap) {
                return 0;
            }
        }
        render(&m);
    }
}

void MoonlightUi::show_error(const std::string &title,
                             const std::string &message) {
    Modal m;
    m.title = title;
    m.message = message;
    m.buttons = {"OK"};
    show_modal(m);
}

bool MoonlightUi::confirm(const std::string &title, const std::string &message,
                          const std::string &yes, const std::string &no,
                          bool danger) {
    Modal m;
    m.title = title;
    m.message = message;
    m.buttons = {yes, no};
    m.selected = danger ? 1 : 0;
    if (danger) {
        m.danger_index = 0;
    }
    return show_modal(m) == 0;
}

int MoonlightUi::menu(const std::string &title,
                      const std::vector<std::string> &items) {
    Modal m;
    m.title = title;
    m.buttons = items;
    m.buttons.push_back("Cancel");
    int r = show_modal(m);
    if (r < 0 || r >= (int)items.size()) {
        return -1;
    }
    return r;
}

bool MoonlightUi::wait_job(const std::shared_ptr<Worker::Job> &job, Modal &m,
                           const std::function<void()> &on_close) {
    m.busy = true;
    int r = show_modal(m, [&job] { return job->is_done(); });
    if (r == -3) {
        // App is closing: still let the job finish so libgamestream state is
        // consistent before teardown. Abort a pairing wait for the PIN
        // (ignored by any other request) so closing does not hang; on_close
        // also covers a pairing job that has not started yet.
        if (on_close && !job->is_done()) {
            on_close();
        }
        http_cancel();
        while (!job->is_done()) {
            svcSleepThread(5 * 1000 * 1000);
        }
        return false;
    }
    return job->is_done();
}

bool MoonlightUi::prompt_text(const std::string &hint,
                              const std::string &initial, std::string &out,
                              size_t max_len, bool allow_empty) {
    std::vector<char> buf(max_len + 1, 0);
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, (int)max_len);
    swkbdSetHintText(&swkbd, hint.c_str());
    if (!initial.empty()) {
        swkbdSetInitialText(&swkbd, initial.c_str());
    }
    swkbdSetValidation(&swkbd,
                       allow_empty ? SWKBD_ANYTHING : SWKBD_NOTEMPTY_NOTBLANK,
                       0, 0);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);
    SwkbdButton pressed = swkbdInputText(&swkbd, buf.data(), buf.size());
    input_reset();
    if (pressed != SWKBD_BUTTON_RIGHT) {
        return false;
    }
    out = buf.data();
    trim(out);
    return allow_empty || !out.empty();
}

bool MoonlightUi::prompt_number(const std::string &hint, int initial,
                                int &out, bool allow_zero) {
    char buf[16] = {0};
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, 8);
    swkbdSetHintText(&swkbd, hint.c_str());
    std::string init = std::to_string(initial);
    swkbdSetInitialText(&swkbd, init.c_str());
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);
    SwkbdButton pressed = swkbdInputText(&swkbd, buf, sizeof(buf));
    input_reset();
    if (pressed != SWKBD_BUTTON_RIGHT) {
        return false;
    }
    int value = atoi(buf);
    if (value < 0 || (value == 0 && !allow_zero)) {
        return false;
    }
    out = value;
    return true;
}

// ---- Computers view -------------------------------------------------------

std::vector<ToolButton> MoonlightUi::pcs_toolbar() const {
    return {
        {ToolButton::Icon::Add, "Add PC", true},
        {ToolButton::Icon::Refresh, "Refresh", !hosts_.empty()},
        {ToolButton::Icon::Options, "Options", !hosts_.empty()},
        {ToolButton::Icon::Settings, "Settings", true},
    };
}

void MoonlightUi::pcs_select(int index) {
    if (hosts_.empty()) {
        sel_pc_ = 0;
        return;
    }
    sel_pc_ = std::max(0, std::min((int)hosts_.size() - 1, index));
    float max_scroll =
        grid_max_scroll((int)hosts_.size(), PC_COLS, PC_Y0, PC_H, PC_GAP);
    scroll_to_row(pc_scroll_, sel_pc_ / PC_COLS, PC_Y0, PC_H, PC_GAP,
                  max_scroll);
}

int MoonlightUi::pcs_hit(float tx, float ty) const {
    return grid_hit(tx, ty, (int)hosts_.size(), PC_COLS, PC_X0, PC_Y0, PC_W,
                    PC_H, PC_GAP, PC_GAP, pc_scroll_);
}

void MoonlightUi::pcs_update(const Input &in) {
    int tb = toolbar_hit(in, 4);
    if (tb == 0) {
        add_pc();
        return;
    } else if (tb == 1 && !hosts_.empty()) {
        refresh_all_hosts(true);
        return;
    } else if (tb == 2 && !hosts_.empty()) {
        pc_options();
        return;
    } else if (tb == 3) {
        settings_enter();
        return;
    }

    if (in.pressed(KEY_START)) {
        settings_enter();
        return;
    }
    if (in.pressed(KEY_SELECT)) {
        add_pc();
        return;
    }

    if (hosts_.empty()) {
        if (in.pressed(KEY_A)) {
            add_pc();
            return;
        }
    } else {
        if (in.pressed(KEY_Y)) {
            refresh_all_hosts(true);
            return;
        }
        if (in.nav & KEY_DLEFT)
            pcs_select(sel_pc_ - 1);
        if (in.nav & KEY_DRIGHT)
            pcs_select(sel_pc_ + 1);
        if (in.nav & KEY_DUP)
            pcs_select(sel_pc_ - PC_COLS);
        if (in.nav & KEY_DDOWN)
            pcs_select(sel_pc_ + PC_COLS);

        if (in.touch_held && in.drag && !in.touch_down &&
            in.start_y >= TOOLBAR_H) {
            float max_scroll = grid_max_scroll((int)hosts_.size(), PC_COLS,
                                               PC_Y0, PC_H, PC_GAP);
            pc_scroll_ =
                std::max(0.0f, std::min(max_scroll, pc_scroll_ - in.dy));
        }
        if (in.tap) {
            int hit = pcs_hit(in.tx, in.ty);
            if (hit >= 0) {
                if (hit == sel_pc_) {
                    connect_to(hosts_[hit]);
                    return;
                }
                pcs_select(hit);
            }
        }
        if (in.pressed(KEY_A)) {
            // Wake-on-LAN is experimental, so it is only offered in the
            // options menu rather than on A
            connect_to(hosts_[sel_pc_]);
            return;
        }
        if (in.pressed(KEY_X)) {
            pc_options();
            return;
        }
    }

    if (in.pressed(KEY_B)) {
        if (confirm("Exit New Moonlight?",
                    "Close New Moonlight and return to the HOME Menu?",
                    "Exit")) {
            quit_ = true;
        }
    }
}

void MoonlightUi::pcs_draw_top() {
    draw_header("New Moonlight", "Computers", true);

    if (hosts_.empty()) {
        // the logo, as on the HOME Menu banner: "new" on the turning wheel
        // beside the title with its red 3
        const float wx = 100.0f, wy = 86.0f;
        logo_wheel(wx, wy, 44.0f);
        silver_text("new", wx, wy - line_height(0.62f) / 2.0f, 0.62f,
                    C2D_AlignCenter);
        logo_text("Moonlight", 158.0f, 50.0f, 1.05f);
        logo_text("for N3DS", 158.0f, 50.0f + line_height(1.05f) - 2.0f,
                  1.05f, 5);
        text("No PCs added yet", TOP_W / 2.0f, 140.0f, 0.5f, color::text,
             C2D_AlignCenter);
        auto lines =
            wrap("Tap  Add PC  on the bottom screen to search the local "
                 "network for PCs running Apollo, Sunshine or GeForce "
                 "Experience, or to enter a PC's IP address.",
                 0.4f, TOP_W - 80.0f);
        text_block(lines, TOP_W / 2.0f, 160.0f, 0.4f, color::text_dim,
                   C2D_AlignCenter);
        draw_hints({{"A", "Add PC"}, {"START", "Settings"}, {"B", "Exit"}});
        return;
    }

    const HostEntry &h = *hosts_[sel_pc_];
    const float cx = 20.0f, cy = 42.0f, cw = TOP_W - 40.0f, ch = 168.0f;
    panel(cx, cy, cw, ch, 8);

    // Big icon with status badge
    float ix = cx + 58.0f, iy = cy + 66.0f;
    bool lit = h.online || !h.status_known;
    icon::desktop(ix, iy, 72, lit ? color::silver : color::text_faint, lit);
    if (h.probing) {
        icon::spinner(ix, icon::desktop_screen_y(iy, 72), 12, now_);
    } else if (h.status_known && !h.online) {
        icon::warning(ix + 26, iy + 14, 30, color::warn, color::black);
    } else if (h.status_known && !h.paired) {
        icon::lock(ix + 26, iy + 14, 30, color::text_dim, color::glass);
    }

    float tx = cx + 120.0f;
    float ty = cy + 16.0f;
    silver_text(ellipsize(h.display_name(), 0.62f, cw - 136.0f), tx, ty, 0.62f);
    ty += line_height(0.62f) + 2.0f;
    text(h.address_text(), tx, ty, 0.42f, color::text_dim);
    ty += line_height(0.42f) + 8.0f;

    if (h.probing || !h.status_known) {
        pill(tx, ty, h.probing ? "Checking status" : "Status unknown", 0.38f,
             color::card_hi, color::text_dim);
    } else if (!h.online) {
        pill(tx, ty, "Offline", 0.38f, color::warn, color::black);
    } else {
        float px = tx;
        px += pill(px, ty, "Online", 0.38f, color::ok, color::black) + 6.0f;
        if (h.paired) {
            px += pill(px, ty, h.current_game ? "Streaming" : "Paired", 0.38f,
                       color::accent_dim, color::text) +
                  6.0f;
        } else {
            pill(px, ty, "Not paired", 0.38f, color::card_hi, color::text_dim);
        }
    }
    ty += line_height(0.38f) + 14.0f;

    if (h.online && !h.server_desc.empty()) {
        text(ellipsize(h.server_desc, 0.38f, cw - 136.0f), tx, ty, 0.38f,
             color::text_faint);
        ty += line_height(0.38f) + 2.0f;
    } else if (!h.online && h.status_known && !h.last_error.empty()) {
        auto lines = wrap(h.last_error, 0.36f, cw - 136.0f);
        if (lines.size() > 2) {
            lines.resize(2);
        }
        ty += text_block(lines, tx, ty, 0.36f, color::text_faint) + 2.0f;
    }

    // Apollo gives every client after the first only "view" and "list"
    // rights; say so here instead of failing at launch with a bare 403.
    std::string perm_warning = permission_warning(h);
    if (!perm_warning.empty()) {
        auto wl = wrap(perm_warning, 0.36f, cw - 156.0f);
        if (wl.size() > 3) {
            wl.resize(3);
        }
        float wy = std::min(ty, cy + ch - 8.0f - wl.size() * line_height(0.36f));
        icon::warning(tx + 7.0f, wy + 7.0f, 13, color::warn, color::black);
        text_block(wl, tx + 20.0f, wy, 0.36f, color::warn);
    }

    std::string action = !h.status_known ? "Connect"
                         : !h.online     ? "Retry"
                         : !h.paired     ? "Pair"
                                         : "Connect";
    draw_hints({{"A", action},
                {"X", "Options"},
                {"Y", "Refresh"},
                {"START", "Settings"}});
}

void MoonlightUi::pcs_draw_bottom() {
    for (size_t i = 0; i < hosts_.size(); i++) {
        int row = (int)i / PC_COLS, col = (int)i % PC_COLS;
        float x = PC_X0 + col * (PC_W + PC_GAP);
        float y = PC_Y0 + row * (PC_H + PC_GAP) - pc_scroll_;
        if (y + PC_H < TOOLBAR_H || y > BOT_H) {
            continue;
        }
        const HostEntry &h = *hosts_[i];
        bool sel = (int)i == sel_pc_;
        u32 card = sel ? color::card_hi : color::card;
        rrect(x, y, PC_W, PC_H, 7, card);
        if (sel) {
            rainbow_frame(x - 3, y - 3, PC_W + 6, PC_H + 6, 10, 2.5f);
        }

        float icx = x + PC_W / 2.0f, icy = y + 32.0f;
        bool lit = h.online || !h.status_known;
        icon::desktop(icx, icy, 38, lit ? color::silver : color::text_faint,
                      lit);
        if (h.probing) {
            icon::spinner(icx, icon::desktop_screen_y(icy, 38), 6.5f, now_);
        } else if (h.status_known && !h.online) {
            icon::warning(icx + 15, icy + 8, 17, color::warn, color::black);
        } else if (h.status_known && !h.paired) {
            icon::lock(icx + 15, icy + 8, 17, color::text_dim, card);
        } else if (h.status_known && h.current_game) {
            circle(icx + 15, icy + 8, 7, color::ok);
            icon::play(icx + 15, icy + 8, 7, color::black);
        }

        text(ellipsize(h.display_name(), 0.45f, PC_W - 16.0f), x + PC_W / 2.0f,
             y + 54.0f, 0.45f, color::text, C2D_AlignCenter);
        text(ellipsize(h.address_text(), 0.36f, PC_W - 16.0f), x + PC_W / 2.0f,
             y + 70.0f, 0.36f, color::text_faint, C2D_AlignCenter);
    }

    if (hosts_.empty()) {
        icon::desktop(BOT_W / 2.0f, 110.0f, 56, color::card_hi, false);
        text("Tap  Add PC  to get started", BOT_W / 2.0f, 150.0f, 0.42f,
             color::text_dim, C2D_AlignCenter);
    }

    draw_toolbar(pcs_toolbar());
}

void MoonlightUi::save_hosts() {
    std::vector<hosts::Record> records;
    for (const auto &h : hosts_) {
        hosts::Record r;
        r.address = h->address;
        r.port = h->port;
        r.name = h->name;
        r.custom_name = h->custom_name;
        r.mac = h->mac;
        records.push_back(r);
    }
    hosts::save(records);
}

void MoonlightUi::probe_host(const std::shared_ptr<HostEntry> &host,
                             bool background) {
    if (host->probing) {
        return;
    }
    host->probing = true;
    auto result = std::make_shared<ProbeResult>();
    std::string key_dir = config_->key_dir;
    bool unsupported = config_->unsupported;
    auto job = Worker::instance().post(
        [host, result, key_dir, unsupported] {
            SERVER_DATA server;
            memset(&server, 0, sizeof(server));
            gs_cleanup();
            http_set_timeout_s(8);
            http_set_connect_timeout_s(4);
            int status = gs_init(&server, (char *)host->address.c_str(),
                                 host->port, key_dir.c_str(), 0, unsupported);
            http_set_timeout_s(60);
            http_set_connect_timeout_s(10);
            if (status == GS_OK) {
                result->ok = true;
                result->paired = server.paired;
                result->current_game = server.currentGame;
                if (server.currentGameUuid && server.currentGameUuid[0]) {
                    result->running_uuid = server.currentGameUuid;
                }
                if (server.hostname && server.hostname[0]) {
                    result->hostname = server.hostname;
                }
                result->desc = describe_server(server);
                result->server = server;
            } else {
                result->error = gs_status_text(status);
            }
        },
        background);
    probes_.push_back({host, job, result});
}

void MoonlightUi::apply_probe(PendingProbe &probe) {
    HostEntry &h = *probe.host;
    const ProbeResult &r = *probe.result;
    h.probing = false;
    h.status_known = true;
    h.online = r.ok;
    if (r.ok) {
        h.paired = r.paired;
        h.current_game = r.current_game;
        h.running_uuid = r.running_uuid;
        resolve_running_app(h);
        // Worker jobs for this host (box art, pairing, launch) may be reading
        // h.server right now. The copy is harmless only because the strings
        // and mode list of the old copy are never freed: they hold the same
        // values and stay valid. Do NOT free the old strings here to fix that
        // small leak; it would turn into a use-after-free on the worker.
        h.server = r.server;
        h.server_valid = true;
        h.server_desc = r.desc;
        h.last_error.clear();
        bool changed = false;
        // The host's own name fills in the card unless the user renamed it
        if (!h.custom_name && !r.hostname.empty() && r.hostname != h.name) {
            h.name = r.hostname;
            changed = true;
        }
        // Remember the MAC so the PC can be woken once it is asleep
        if (r.server.mac && wol::valid_mac(r.server.mac) &&
            h.mac != r.server.mac) {
            h.mac = r.server.mac;
            changed = true;
        }
        if (changed) {
            save_hosts();
        }
        if (h.paired) {
            add_pair_address(h.address, h.port);
        }
    } else {
        h.server_valid = false;
        h.last_error = r.error;
    }
}

bool MoonlightUi::probe_host_blocking(const std::shared_ptr<HostEntry> &host,
                                      const std::string &title) {
    if (!host->probing) {
        probe_host(host, false);
    }
    std::shared_ptr<Worker::Job> job;
    for (const auto &p : probes_) {
        if (p.host == host) {
            job = p.job;
            break;
        }
    }
    if (!job) {
        return host->online;
    }
    Modal m;
    m.title = title;
    m.busy_text = host->address_text();
    if (!wait_job(job, m)) {
        return false;
    }
    // The job may have finished between pump() and the completion check.
    pump();
    return host->online;
}

void MoonlightUi::refresh_all_hosts(bool interactive) {
    for (auto &h : hosts_) {
        probe_host(h, true);
    }
    if (interactive) {
        toast("Refreshing PC status");
    }
}

void MoonlightUi::add_pc() {
    int choice = menu("Add PC", {"Search the local network", "Enter IP address"});
    if (choice == 0) {
        discover_pcs();
    } else if (choice == 1) {
        add_pc_manual();
    }
}

void MoonlightUi::add_pc_manual() {
    std::string input;
    if (!prompt_text("IP address of the host PC (e.g. 192.168.1.10)", "",
                     input)) {
        return;
    }
    std::string address;
    uint16_t port;
    if (!hosts::split_address(input, address, port)) {
        show_error("Invalid address", "Please enter an IP address or host name, "
                                      "optionally followed by :port.");
        return;
    }
    for (size_t i = 0; i < hosts_.size(); i++) {
        if (hosts_[i]->address == address && hosts_[i]->port == port) {
            pcs_select((int)i);
            toast("That PC is already in the list");
            return;
        }
    }

    auto host = std::make_shared<HostEntry>();
    host->address = address;
    host->port = port;
    add_host_entry(host);
}

namespace {
struct DiscoveredPc {
    std::string address;
    std::string name;
    bool online = false;
    bool paired = false;
};
} // namespace

void MoonlightUi::discover_pcs() {
    auto result = std::make_shared<std::vector<DiscoveredPc>>();
    std::string key_dir = config_->key_dir;
    bool unsupported = config_->unsupported;
    auto job = Worker::instance().post([result, key_dir, unsupported] {
        auto addresses = discovery::scan_local_network();
        for (const auto &address : addresses) {
            DiscoveredPc pc;
            pc.address = address;
            SERVER_DATA server;
            memset(&server, 0, sizeof(server));
            gs_cleanup();
            http_set_timeout_s(5);
            http_set_connect_timeout_s(3);
            int status = gs_init(&server, (char *)pc.address.c_str(), 47989,
                                 key_dir.c_str(), 0, unsupported);
            http_set_timeout_s(60);
            http_set_connect_timeout_s(10);
            if (status == GS_OK) {
                pc.online = true;
                pc.paired = server.paired;
                if (server.hostname && server.hostname[0]) {
                    pc.name = server.hostname;
                }
                result->push_back(pc);
            }
        }
    });

    Modal m;
    m.title = "Searching for PCs";
    m.busy_text = "Scanning your network for PCs running Sunshine or GeForce "
                  "Experience. This takes a few seconds.";
    if (!wait_job(job, m)) {
        return;
    }

    if (result->empty()) {
        Modal none;
        none.title = "No PCs found";
        none.message =
            "No GameStream hosts answered on this network. Make sure Sunshine "
            "or GeForce Experience is running and the PC is on the same Wi-Fi "
            "network, or add the PC by IP address.";
        none.buttons = {"Enter IP address", "Cancel"};
        if (show_modal(none) == 0) {
            add_pc_manual();
        }
        return;
    }

    // Pick list: host name on the button, IP address underneath, and a
    // badge for the pairing / already-added state.
    Modal pick;
    pick.title = "Found " + std::to_string(result->size()) +
                 (result->size() == 1 ? " PC" : " PCs");
    pick.message = "Select the PC to add. PCs already in your list are "
                   "marked.";
    pick.list = true;
    std::vector<int> existing;
    for (const auto &pc : *result) {
        int idx = -1;
        for (size_t i = 0; i < hosts_.size(); i++) {
            if (hosts_[i]->address == pc.address) {
                idx = (int)i;
                break;
            }
        }
        pick.buttons.push_back(pc.name.empty() ? "Unnamed PC" : pc.name);
        pick.subtitles.push_back(pc.address);
        pick.badges.push_back(idx >= 0      ? "Already added"
                              : pc.paired ? "Paired"
                                          : "Not paired");
        existing.push_back(idx);
    }
    pick.buttons.push_back("Cancel");
    pick.subtitles.push_back("");
    pick.badges.push_back("");

    int choice = show_modal(pick);
    if (choice < 0 || choice >= (int)result->size()) {
        return;
    }
    if (existing[choice] >= 0) {
        pcs_select(existing[choice]);
        toast("That PC is already in the list");
        return;
    }

    const DiscoveredPc &pc = (*result)[choice];
    auto host = std::make_shared<HostEntry>();
    host->address = pc.address;
    host->port = 47989;
    host->name = pc.name;
    add_host_entry(host);
}

void MoonlightUi::add_host_entry(const std::shared_ptr<HostEntry> &host) {
    if (!probe_host_blocking(host, "Connecting to " + host->display_name())) {
        std::string msg = "Unable to connect to the specified PC.";
        if (!host->last_error.empty()) {
            msg += "\n" + host->last_error;
        }
        msg += "\nMake sure Sunshine or GeForce Experience is running and "
               "that the 3DS is on the same network.";
        Modal m;
        m.title = "Connection failed";
        m.message = msg;
        m.buttons = {"Add anyway", "Cancel"};
        m.selected = 1;
        if (show_modal(m) != 0) {
            return;
        }
    }
    hosts_.push_back(host);
    save_hosts();
    pcs_select((int)hosts_.size() - 1);
    toast("Added " + host->display_name());

    if (host->online && !host->paired) {
        if (confirm("Pair with " + host->display_name() + "?",
                    "The PC is online but not paired with this 3DS yet. Pair "
                    "now?",
                    "Pair")) {
            pair_host(host);
        }
    }
}

void MoonlightUi::pc_options() {
    if (hosts_.empty()) {
        return;
    }
    auto host = hosts_[sel_pc_];
    std::vector<std::string> items;
    std::vector<int> actions;
    enum {
        ACT_CONNECT,
        ACT_RESUME,
        ACT_WAKE,
        ACT_PAIR,
        ACT_PAIR_OTP,
        ACT_UNPAIR,
        ACT_RENAME,
        ACT_REFRESH,
        ACT_DELETE,
        ACT_WOL_GUIDE
    };
    items.push_back("Connect");
    actions.push_back(ACT_CONNECT);
    if (host->status_known && host->online && host->paired &&
        host->current_game != 0) {
        items.push_back("Resume running app");
        actions.push_back(ACT_RESUME);
    }
    if (!(host->status_known && host->online) && wol::valid_mac(host->mac)) {
        items.push_back("Wake PC (experimental)");
        actions.push_back(ACT_WAKE);
    }
    if (host->status_known && host->online) {
        items.push_back(host->paired ? "Unpair" : "Pair");
        actions.push_back(host->paired ? ACT_UNPAIR : ACT_PAIR);
        // Apollo can also pair from a PIN + passphrase made in its web UI
        if (!host->paired && host->server_valid && host->server.isApollo) {
            items.push_back("Pair with one-time PIN");
            actions.push_back(ACT_PAIR_OTP);
        }
    }
    items.push_back("Rename");
    actions.push_back(ACT_RENAME);
    items.push_back("Refresh status");
    actions.push_back(ACT_REFRESH);
    items.push_back("Delete PC");
    actions.push_back(ACT_DELETE);
    items.push_back("Wake-on-LAN setup guide");
    actions.push_back(ACT_WOL_GUIDE);

    int choice = menu(host->display_name(), items);
    if (choice < 0) {
        return;
    }
    switch (actions[choice]) {
    case ACT_CONNECT:
        connect_to(host);
        break;
    case ACT_RESUME:
        resume_running(host);
        break;
    case ACT_WAKE:
        wake_host(host);
        break;
    case ACT_WOL_GUIDE:
        show_wol_guide();
        break;
    case ACT_PAIR:
        pair_host(host);
        break;
    case ACT_PAIR_OTP:
        pair_host(host, true);
        break;
    case ACT_UNPAIR:
        unpair_host(host);
        break;
    case ACT_RENAME:
        rename_host(host);
        break;
    case ACT_REFRESH:
        probe_host(host, false);
        break;
    case ACT_DELETE:
        delete_host(host);
        break;
    }
}

void MoonlightUi::rename_host(const std::shared_ptr<HostEntry> &host) {
    std::string name;
    if (!prompt_text("Name for this PC (empty = the PC's own name)",
                     host->display_name(), name, 40, true)) {
        return;
    }
    if (name.empty()) {
        // Back to the name the PC reports; the next status check fills it in
        // if it is not known yet
        host->custom_name = false;
        host->name = (host->server_valid && host->server.hostname &&
                      host->server.hostname[0])
                         ? host->server.hostname
                         : "";
        probe_host(host, true);
    } else if (name != host->display_name()) {
        host->name = name;
        host->custom_name = true;
    } else {
        return; // unchanged
    }
    save_hosts();
}

void MoonlightUi::delete_host(const std::shared_ptr<HostEntry> &host) {
    if (!confirm("Delete " + host->display_name() + "?",
                 "The PC is removed from this list. Pairing on the host PC "
                 "is not affected.",
                 "Delete", "Cancel", true)) {
        return;
    }
    host->deleted = true;
    hosts_.erase(std::remove(hosts_.begin(), hosts_.end(), host), hosts_.end());
    remove_pair_address(host->address, host->port);
    save_hosts();
    pcs_select(sel_pc_);
    toast("Deleted " + host->display_name());
}

std::string MoonlightUi::permission_warning(const HostEntry &host) {
    if (!host.server_valid || !host.online || !host.paired ||
        !host.server.isApollo) {
        return "";
    }
    unsigned int perm = host.server.permission;
    if (!(perm & APOLLO_PERM_LIST) && !(perm & APOLLO_PERM_LAUNCH)) {
        return "Apollo gives this 3DS view-only access: it cannot list or "
               "launch apps. Grant permissions on the PIN tab of Apollo's web "
               "UI.";
    }
    if (!(perm & APOLLO_PERM_LAUNCH)) {
        return "This 3DS may not launch apps. Grant \"Launch Apps\" on the "
               "PIN tab of Apollo's web UI.";
    }
    if (!(perm & APOLLO_PERM_ALL_INPUTS)) {
        return "This 3DS has no input permission, so buttons and touch will "
               "be ignored. Grant them on the PIN tab of Apollo's web UI.";
    }
    return "";
}

void MoonlightUi::pair_host(const std::shared_ptr<HostEntry> &host,
                            bool one_time_pin) {
    if (!host->server_valid || !host->online) {
        if (!probe_host_blocking(host,
                                 "Connecting to " + host->display_name())) {
            show_error("Connection failed", host->last_error.empty()
                                                ? "Unable to reach the PC."
                                                : host->last_error);
            return;
        }
    }
    if (host->paired) {
        toast("Already paired with " + host->display_name());
        return;
    }

    std::string pin;
    std::string passphrase;
    if (one_time_pin) {
        // Apollo's PIN tab can generate a one-time PIN and passphrase; the
        // host then accepts the pairing without anyone typing on the PC.
        if (!prompt_text("One-time PIN shown by Apollo (4 digits)", "", pin,
                         8)) {
            return;
        }
        pin.erase(std::remove_if(pin.begin(), pin.end(),
                                 [](unsigned char c) { return !isdigit(c); }),
                  pin.end());
        if (pin.size() != 4) {
            show_error("Invalid PIN", "The one-time PIN is four digits.");
            return;
        }
        if (!prompt_text("Passphrase shown next to the PIN", "", passphrase,
                         64) ||
            passphrase.empty()) {
            return;
        }
    } else {
        char pin_buf[8];
        snprintf(pin_buf, sizeof(pin_buf), "%04u",
                 (unsigned)(rand() % 10000));
        pin = pin_buf;
    }

    auto status = std::make_shared<int>(GS_FAILED);
    auto error = std::make_shared<std::string>();
    // Set by Cancel. The job checks it after opening the cancel window, so a
    // Cancel pressed while the job is still queued behind a probe counts too
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    auto job = Worker::instance().post([host, pin, passphrase, status, error,
                                        cancelled] {
        // Pairing waits for the user to type the PIN on the PC
        http_set_timeout_s(3 * 60);
        http_set_connect_timeout_s(10);
        std::string pin_copy = pin;
        // The dialog's Cancel button aborts the wait for the PIN
        http_begin_cancellable();
        if (cancelled->load()) {
            http_end_cancellable();
            http_set_timeout_s(60);
            *error = "Cancelled";
            return;
        }
        *status = gs_pair(&host->server, &pin_copy[0],
                          passphrase.empty() ? nullptr : passphrase.c_str());
        http_end_cancellable();
        http_set_timeout_s(60);
        if (*status != GS_OK) {
            *error = gs_error ? gs_error : "Pairing failed";
        }
    });

    Modal m;
    m.title = "Pairing with " + host->display_name();
    if (one_time_pin) {
        m.message =
            "Checking the one-time PIN and passphrase with the PC. They "
            "expire a few minutes after Apollo generates them, so make new "
            "ones if this fails.";
        m.busy_text = "Pairing with the one-time PIN";
    } else {
        m.message =
            "Enter this PIN on the host PC. With Sunshine or Apollo, open the "
            "web UI and use the PIN page. This dialog closes when pairing "
            "completes.";
        m.big_text = pin;
        m.busy_text = "Waiting for the PIN to be entered on the PC";
    }
    m.buttons = {"Cancel"};
    // Closing the app while the job is still queued must cancel it too
    auto cancel_on_close = [cancelled] { cancelled->store(true); };
    if (!wait_job(job, m, cancel_on_close)) {
        if (!job->is_done()) {
            // Cancel (or B): abort the request and wait for the worker to
            // let go, which takes at most about a second
            cancelled->store(true);
            http_cancel();
            Modal c;
            c.title = "Cancelling pairing";
            wait_job(job, c, cancel_on_close);
            if (*status != GS_OK) {
                toast("Pairing cancelled");
                return;
            }
            // Pairing finished just before the cancel landed; keep it
        } else {
            return;
        }
    }

    if (*status == GS_OK) {
        host->paired = true;
        host->server.paired = true;
        add_pair_address(host->address, host->port);
        // Apollo only reports permissions, server commands and the MAC to a
        // paired client, so check the PC again now that it is paired
        probe_host_blocking(host, "Paired with " + host->display_name());
        toast("Paired with " + host->display_name());
    } else {
        show_error("Pairing failed", *error);
    }
}

void MoonlightUi::unpair_host(const std::shared_ptr<HostEntry> &host) {
    // Only GeForce Experience implements the client-initiated /unpair
    // request; Sunshine (and forks such as Apollo) remove paired devices
    // from their web UI instead.
    if (host->server_valid && !host->server.isNvidiaSoftware) {
        Modal m;
        m.title = "Unpair from " + host->display_name();
        m.message =
            "Sunshine and Apollo hosts can only be unpaired from the host "
            "PC: open the web UI, go to the PIN page and remove this device "
            "(\"New 3DS\") from the paired clients, then refresh here.";
        m.buttons = {"OK"};
        show_modal(m);
        return;
    }
    if (!confirm("Unpair from " + host->display_name() + "?",
                 "You will need to pair again before streaming.", "Unpair",
                 "Cancel", true)) {
        return;
    }
    auto status = std::make_shared<int>(GS_FAILED);
    auto error = std::make_shared<std::string>();
    auto job = Worker::instance().post([host, status, error] {
        http_set_timeout_s(15);
        *status = gs_unpair(&host->server);
        http_set_timeout_s(60);
        if (*status != GS_OK) {
            *error = gs_error ? gs_error : "Unpair failed";
        }
    });
    Modal m;
    m.title = "Unpairing";
    m.busy_text = host->display_name();
    if (!wait_job(job, m)) {
        return;
    }
    if (*status == GS_OK) {
        host->paired = false;
        host->server.paired = false;
        remove_pair_address(host->address, host->port);
        toast("Unpaired from " + host->display_name());
    } else {
        show_error("Unpair failed", *error);
    }
}

void MoonlightUi::wake_host(const std::shared_ptr<HostEntry> &host) {
    if (!wol::valid_mac(host->mac)) {
        show_error("Cannot wake " + host->display_name(),
                   "Moonlight learns the PC's network address the first time "
                   "it connects while paired. Connect once while the PC is "
                   "on, then Wake PC will be available.");
        return;
    }
    // Untested on real hardware so far; say so up front
    if (!confirm("Wake " + host->display_name() + "? (experimental)",
                 "Wake-on-LAN is untested. It only works once it is enabled "
                 "in the PC's BIOS and network adapter settings; see the "
                 "Wake-on-LAN setup guide in this PC's options menu.",
                 "Wake PC", "Cancel")) {
        return;
    }
    if (!wol::send(host->mac, host->address)) {
        show_error("Cannot wake " + host->display_name(),
                   "The wake-up packet could not be sent. Check the 3DS's "
                   "Wi-Fi connection.");
        return;
    }

    // Resend the packet a few times and probe until the host answers, for up
    // to 90 seconds. Wake-on-LAN must be enabled in the PC's BIOS and network
    // adapter settings for this to work at all.
    const u64 started = osGetTime();
    const u64 deadline = started + 90 * 1000;
    u64 next_probe = started + 4000;
    u64 next_wake = started + 10 * 1000;
    Modal m;
    m.title = "Waking " + host->display_name();
    m.busy = true;
    m.busy_text = "Sent the wake-up packet, waiting for the PC";
    m.buttons = {"Cancel"};
    bool timed_out = false;
    int r = show_modal(m, [&] {
        u64 now = osGetTime();
        if (host->status_known && host->online) {
            return true;
        }
        if (now >= next_wake) {
            wol::send(host->mac, host->address);
            next_wake = now + 10 * 1000;
        }
        if (now >= next_probe && !host->probing) {
            probe_host(host, false);
            next_probe = now + 4000;
            m.busy_text = "Waiting for the PC (" +
                          std::to_string((now - started) / 1000) + " s)";
        }
        if (now >= deadline) {
            timed_out = true;
            return true;
        }
        return false;
    });
    if (r != -2) {
        return; // cancelled
    }
    if (host->online) {
        toast(host->display_name() + " is awake");
        connect_to(host);
    } else if (timed_out) {
        if (confirm("Still asleep",
                    host->display_name() +
                        " did not answer within 90 seconds. Wake-on-LAN "
                        "has to be enabled in the PC's BIOS and network "
                        "adapter settings first.",
                    "Open setup guide", "Close")) {
            show_wol_guide();
        }
    }
}

void MoonlightUi::resume_running(const std::shared_ptr<HostEntry> &host) {
    if (!host->server_valid || !host->online) {
        if (!probe_host_blocking(host,
                                 "Connecting to " + host->display_name())) {
            show_error("Connection failed", host->last_error.empty()
                                                ? "Unable to reach the PC."
                                                : host->last_error);
            return;
        }
    }
    if (!host->paired || host->current_game == 0) {
        toast("Nothing is running on " + host->display_name());
        return;
    }
    if (!load_apps(host, true)) {
        return;
    }
    cur_host_ = host;
    app_scroll_ = 0;
    app_name_lines_for_ = -1;
    view_ = View::Apps;
    for (size_t i = 0; i < apps_.size(); i++) {
        if (apps_[i].id == host->current_game) {
            apps_select((int)i);
            launch_app((int)i);
            return;
        }
    }
    toast("The running app is not in the list");
}

void MoonlightUi::maybe_resume_on_start() {
    if (!config_->resume_on_start || hosts_.empty()) {
        return;
    }
    // Give the start-up status checks a few seconds to come back
    u64 deadline = osGetTime() + 6000;
    Modal m;
    m.title = "Resume on start";
    m.busy = true;
    m.busy_text = "Checking your PCs for a running app";
    m.buttons = {"Skip"};
    int r = show_modal(m, [&] {
        if (osGetTime() > deadline) {
            return true;
        }
        for (const auto &h : hosts_) {
            if (h->probing || !h->status_known) {
                return false;
            }
        }
        return true;
    });
    if (r != -2 || quit_) {
        return;
    }
    std::shared_ptr<HostEntry> target;
    for (const auto &h : hosts_) {
        if (h->status_known && h->online && h->paired && h->current_game) {
            target = h;
            break;
        }
    }
    if (!target) {
        return;
    }
    // Short countdown so the launcher can still be reached
    u64 go_at = osGetTime() + 3000;
    Modal c;
    c.title = "Resuming on " + target->display_name();
    c.busy = true;
    c.buttons = {"Stay in launcher"};
    r = show_modal(c, [&] {
        u64 now = osGetTime();
        c.busy_text = "Resuming the running app in " +
                      std::to_string((go_at - std::min(now, go_at) + 999) /
                                     1000) +
                      "...";
        return now >= go_at;
    });
    if (r == -2 && !quit_) {
        pcs_select((int)(std::find(hosts_.begin(), hosts_.end(), target) -
                         hosts_.begin()));
        resume_running(target);
    }
}

void MoonlightUi::connect_to(const std::shared_ptr<HostEntry> &host) {
    if (!host->server_valid || !host->online) {
        if (!probe_host_blocking(host,
                                 "Connecting to " + host->display_name())) {
            std::string msg = host->last_error.empty()
                                  ? "Unable to connect to the PC."
                                  : host->last_error;
            msg += "\nMake sure the PC is awake and Sunshine or GeForce "
                   "Experience is running.";
            show_error("Connection failed", msg);
            return;
        }
    }
    if (!host->paired) {
        if (confirm("Pair with " + host->display_name() + "?",
                    "You must pair with the PC before streaming.", "Pair")) {
            pair_host(host);
        }
        if (!host->paired) {
            return;
        }
    }
    if (!load_apps(host, true)) {
        return;
    }
    cur_host_ = host;
    sel_app_ = 0;
    app_scroll_ = 0;
    app_name_lines_for_ = -1;
    view_ = View::Apps;
}

// ---- Apps view ------------------------------------------------------------

std::vector<ToolButton> MoonlightUi::apps_toolbar() const {
    bool running = cur_host_ && cur_host_->current_game != 0;
    return {
        {ToolButton::Icon::Back, "Back", true},
        {ToolButton::Icon::Refresh, "Refresh", true},
        {ToolButton::Icon::Stop, "Quit app", running},
        {ToolButton::Icon::Settings, "Settings", true},
    };
}

void MoonlightUi::apps_select(int index) {
    if (apps_.empty()) {
        sel_app_ = 0;
        return;
    }
    sel_app_ = std::max(0, std::min((int)apps_.size() - 1, index));
    float max_scroll = grid_max_scroll((int)apps_.size(), APP_COLS, APP_Y0,
                                       APP_H, APP_GAP_Y);
    scroll_to_row(app_scroll_, sel_app_ / APP_COLS, APP_Y0, APP_H, APP_GAP_Y,
                  max_scroll);
}

int MoonlightUi::apps_hit(float tx, float ty) const {
    return grid_hit(tx, ty, (int)apps_.size(), APP_COLS, APP_X0, APP_Y0, APP_W,
                    APP_H, APP_GAP_X, APP_GAP_Y, app_scroll_);
}

void MoonlightUi::resolve_running_app(HostEntry &host) {
    if (host.current_game > 0 || host.running_uuid.empty()) {
        return;
    }
    // Apollo's Remote Input reports a real ID; anything else found by UUID
    host.current_game = -1; // running, ID unknown until the list is loaded
    if (cur_host_.get() != &host) {
        return;
    }
    for (const auto &a : apps_) {
        if (!a.uuid.empty() &&
            strcasecmp(a.uuid.c_str(), host.running_uuid.c_str()) == 0) {
            host.current_game = a.id;
            return;
        }
    }
}

std::string MoonlightUi::running_app_name() const {
    if (!cur_host_ || cur_host_->current_game == 0) {
        return "";
    }
    for (const auto &a : apps_) {
        if (a.id == cur_host_->current_game) {
            return a.name;
        }
    }
    return "another app";
}

void MoonlightUi::apps_update(const Input &in) {
    int tb = toolbar_hit(in, 4);
    if (tb == 0 || in.pressed(KEY_B)) {
        leave_apps();
        return;
    } else if (tb == 1 || in.pressed(KEY_Y)) {
        load_apps(cur_host_, false);
        probe_host(cur_host_, true);
        toast("Refreshing");
        return;
    } else if ((tb == 2 || in.pressed(KEY_X)) && cur_host_->current_game) {
        if (confirm("Quit " + running_app_name() + "?",
                    "This closes the app running on " +
                        cur_host_->display_name() + ".",
                    "Quit app", "Cancel", true)) {
            quit_running_app(cur_host_);
        }
        return;
    } else if (tb == 3 || in.pressed(KEY_START)) {
        settings_enter();
        return;
    }

    if (apps_.empty()) {
        return;
    }
    if (in.nav & KEY_DLEFT)
        apps_select(sel_app_ - 1);
    if (in.nav & KEY_DRIGHT)
        apps_select(sel_app_ + 1);
    if (in.nav & KEY_DUP)
        apps_select(sel_app_ - APP_COLS);
    if (in.nav & KEY_DDOWN)
        apps_select(sel_app_ + APP_COLS);

    if (in.touch_held && in.drag && !in.touch_down && in.start_y >= TOOLBAR_H) {
        float max_scroll = grid_max_scroll((int)apps_.size(), APP_COLS, APP_Y0,
                                           APP_H, APP_GAP_Y);
        app_scroll_ = std::max(0.0f, std::min(max_scroll, app_scroll_ - in.dy));
    }
    if (in.tap) {
        int hit = apps_hit(in.tx, in.ty);
        if (hit >= 0) {
            if (hit == sel_app_) {
                launch_app(hit);
                return;
            }
            apps_select(hit);
        }
    }
    if (in.pressed(KEY_A)) {
        launch_app(sel_app_);
        return;
    }
}

void MoonlightUi::apps_draw_top() {
    std::string subtitle =
        apps_.empty() ? "" : std::to_string(apps_.size()) + " apps";
    if (pending_apps_) {
        subtitle = "Refreshing";
    }
    draw_header(cur_host_ ? cur_host_->display_name() : "Apps", subtitle,
                false);

    if (apps_.empty()) {
        text(apps_denied_ ? "Not allowed to list apps" : "No apps found",
             TOP_W / 2.0f, 90.0f, 0.55f, color::text, C2D_AlignCenter);
        auto lines = wrap(apps_denied_
                              ? "Apollo gives new clients view-only access. "
                                "On the PIN tab of Apollo's web UI, give "
                                "\"New 3DS\" the List Apps and Launch Apps "
                                "permissions, then press Y to refresh."
                              : "Add apps in the Sunshine web UI under "
                                "Applications, then press Y to refresh.",
                          0.42f, TOP_W - 80.0f);
        text_block(lines, TOP_W / 2.0f, 118.0f, 0.42f, color::text_dim,
                   C2D_AlignCenter);
        draw_hints({{"Y", "Refresh"}, {"B", "Back"}});
        return;
    }

    const AppEntry &app = apps_[sel_app_];
    bool running = cur_host_ && cur_host_->current_game == app.id;

    const float cx = 20.0f, cy = 42.0f, cw = TOP_W - 40.0f, ch = 168.0f;
    panel(cx, cy, cw, ch, 8);

    // Box art at native 96x128
    float ax = cx + 18.0f, ay = cy + 20.0f;
    rrect(ax - 3, ay - 3, boxart::IMG_W + 6, boxart::IMG_H + 6, 5,
          running ? color::ok : color::card_hi);
    if (app.builtin != AppEntry::Builtin::None) {
        builtin_art(app.builtin, ax, ay, boxart::IMG_W, boxart::IMG_H);
    } else if (app.art_state == AppEntry::ArtState::Ready) {
        boxart::draw(app.art, ax, ay, 1.0f);
    } else {
        rect(ax, ay, boxart::IMG_W, boxart::IMG_H, color::card);
        if (app.art_state == AppEntry::ArtState::Loading) {
            icon::spinner(ax + boxart::IMG_W / 2.0f, ay + boxart::IMG_H / 2.0f,
                          12, now_);
        } else {
            icon::gamepad(ax + boxart::IMG_W / 2.0f, ay + boxart::IMG_H / 2.0f,
                          56, color::card_hi, color::text_faint);
        }
    }

    float tx = ax + boxart::IMG_W + 22.0f;
    float tw = cx + cw - tx - 16.0f;
    float ty = cy + 18.0f;
    if (app_name_lines_for_ != sel_app_) {
        app_name_lines_ = wrap(app.name, 0.6f, tw);
        if (app_name_lines_.size() > 2) {
            app_name_lines_.resize(2);
            app_name_lines_[1] = ellipsize(app_name_lines_[1] + "\xE2\x80\xA6",
                                           0.6f, tw);
        }
        app_name_lines_for_ = sel_app_;
    }
    ty += text_block(app_name_lines_, tx, ty, 0.6f, color::text) + 6.0f;

    if (running) {
        float pw = pill(tx, ty, "Running", 0.38f, color::ok, color::black);
        (void)pw;
    } else if (app.input_only) {
        // Apollo's Remote Input: no video, the 3DS just acts as a controller
        pill(tx, ty, "Input only, no video", 0.38f, color::accent_dim,
             color::text);
    } else if (cur_host_ && cur_host_->current_game) {
        pill(tx, ty, "Another app is running", 0.38f, color::card_hi,
             color::text_dim);
    } else {
        pill(tx, ty, "Ready to stream", 0.38f, color::card_hi, color::text_dim);
    }
    ty += line_height(0.38f) + 16.0f;

    std::string res = std::to_string(config_->stream.width) + "x" +
                      std::to_string(config_->stream.height);
    if (config_->host_width > 0 && config_->host_height > 0) {
        res += " (host " + std::to_string(config_->host_width) + "x" +
               std::to_string(config_->host_height) + ")";
    }
    res += "  " + std::to_string(config_->stream.fps) + " fps  " +
           std::to_string(config_->stream.bitrate / 1000) + " Mbps";
    text(res, tx, ty, 0.36f, color::text_faint);
    ty += line_height(0.36f) + 2.0f;
    text(cur_host_ ? cur_host_->address_text() : "", tx, ty, 0.36f,
         color::text_faint);

    std::vector<std::pair<const char *, std::string>> hints;
    hints.push_back({"A", running ? "Resume" : "Launch"});
    if (cur_host_ && cur_host_->current_game) {
        hints.push_back({"X", "Quit app"});
    }
    hints.push_back({"Y", "Refresh"});
    hints.push_back({"B", "Back"});
    draw_hints(hints);
}

void MoonlightUi::apps_draw_bottom() {
    for (size_t i = 0; i < apps_.size(); i++) {
        int row = (int)i / APP_COLS, col = (int)i % APP_COLS;
        float x = APP_X0 + col * (APP_W + APP_GAP_X);
        float y = APP_Y0 + row * (APP_H + APP_GAP_Y) - app_scroll_;
        if (y + APP_H < TOOLBAR_H || y > BOT_H) {
            continue;
        }
        const AppEntry &app = apps_[i];
        bool sel = (int)i == sel_app_;
        bool running = cur_host_ && cur_host_->current_game == app.id;

        float art_w = boxart::IMG_W * ART_SCALE;
        float art_h = boxart::IMG_H * ART_SCALE;
        float ax = x + (APP_W - art_w) / 2.0f;
        float ay = y + 3.0f;
        if (running && !sel) {
            rrect(ax - 2, ay - 2, art_w + 4, art_h + 4, 4, color::ok);
        }
        if (app.builtin != AppEntry::Builtin::None) {
            builtin_art(app.builtin, ax, ay, art_w, art_h);
        } else if (app.art_state == AppEntry::ArtState::Ready) {
            boxart::draw(app.art, ax, ay, ART_SCALE);
        } else {
            rect(ax, ay, art_w, art_h, sel ? color::card_hi : color::card);
            if (app.art_state == AppEntry::ArtState::Loading) {
                icon::spinner(ax + art_w / 2.0f, ay + art_h / 2.0f, 8, now_);
            } else {
                icon::gamepad(ax + art_w / 2.0f, ay + art_h / 2.0f, 36,
                              sel ? color::card : color::card_hi,
                              color::text_faint);
            }
        }
        if (sel) {
            rainbow_frame(ax - 4, ay - 4, art_w + 8, art_h + 8, 6, 2.5f);
        }
        if (running) {
            circle(ax + art_w - 8, ay + 8, 8, color::ok);
            icon::play(ax + art_w - 8, ay + 8, 8, color::black);
        }
        text(app.tile_name, x + APP_W / 2.0f, ay + art_h + 4.0f, 0.36f,
             sel ? color::text : color::text_dim, C2D_AlignCenter);
    }

    if (apps_.empty()) {
        if (pending_apps_) {
            icon::spinner(BOT_W / 2.0f, BOT_H / 2.0f, 18.0f, now_);
            text("Loading apps", BOT_W / 2.0f, BOT_H / 2.0f + 30.0f, 0.42f,
                 color::text_dim, C2D_AlignCenter);
        } else {
            text("No apps", BOT_W / 2.0f, 120.0f, 0.45f, color::text_dim,
                 C2D_AlignCenter);
        }
    }

    draw_toolbar(apps_toolbar());
}

bool MoonlightUi::load_apps(const std::shared_ptr<HostEntry> &host,
                            bool interactive) {
    auto result = std::make_shared<AppsResult>();
    auto job = Worker::instance().post(
        [host, result] {
            PAPP_LIST list = NULL;
            http_set_timeout_s(20);
            result->status = gs_applist(&host->server, &list);
            http_set_timeout_s(60);
            if (result->status != GS_OK) {
                result->error = gs_status_text(result->status);
            }
            bool host_order = false;
            while (list != NULL) {
                PAPP_LIST next = list->next;
                AppInfo info;
                info.id = list->id;
                info.name = list->name ? std::string(list->name) : "";
                info.uuid = list->uuid ? std::string(list->uuid) : "";
                info.idx = list->idx;
                host_order = host_order || info.idx > 0;
                // Apollo's "Terminate" entry (Input Only Mode) quits the
                // running app; the Quit app button does the same, so leave
                // it out of the grid
                if (strcasecmp(info.uuid.c_str(), APOLLO_TERMINATE_UUID) !=
                    0) {
                    result->apps.push_back(info);
                }
                free(list->name);
                free(list->uuid);
                free(list);
                list = next;
            }
            // Apollo answers a client without the List Apps permission with
            // one placeholder entry.
            if (result->apps.size() == 1 &&
                result->apps[0].id == APOLLO_PERMISSION_DENIED_APP_ID) {
                result->denied = true;
                result->apps.clear();
            }
            if (host_order) {
                // Apollo sends the order the user arranged in its web UI
                std::sort(result->apps.begin(), result->apps.end(),
                          [](const AppInfo &a, const AppInfo &b) {
                              return a.idx < b.idx;
                          });
            } else {
                std::sort(result->apps.begin(), result->apps.end(),
                          [](const AppInfo &a, const AppInfo &b) {
                              return strcasecmp(a.name.c_str(),
                                                b.name.c_str()) < 0;
                          });
            }
        },
        !interactive);

    if (!interactive) {
        pending_apps_.reset(new PendingApps{host, job, result});
        return true;
    }

    Modal m;
    m.title = "Loading apps";
    m.busy_text = host->display_name();
    if (!wait_job(job, m)) {
        return false;
    }
    if (result->status != GS_OK) {
        show_error("Unable to load apps", result->error);
        return false;
    }
    apply_apps(host, *result);
    return true;
}

void MoonlightUi::apply_apps(const std::shared_ptr<HostEntry> &host,
                             const AppsResult &result) {
    Worker::instance().drop_background();
    std::vector<AppEntry> fresh;
    fresh.reserve(result.apps.size());
    apps_denied_ = result.denied;
    for (const auto &entry : result.apps) {
        AppEntry app;
        app.id = entry.id;
        app.name = entry.name;
        app.uuid = entry.uuid;
        app.input_only = strcasecmp(entry.uuid.c_str(),
                                    APOLLO_REMOTE_INPUT_UUID) == 0;
        app.builtin = builtin_kind(entry.name, entry.uuid);
        app.tile_name = ellipsize(app.name, 0.36f, APP_W - 4.0f);
        // Keep already-loaded art for apps that are still present
        for (auto &old : apps_) {
            if (old.id == app.id &&
                old.art_state == AppEntry::ArtState::Ready) {
                app.art = old.art;
                app.art_state = AppEntry::ArtState::Ready;
                old.art.valid = false; // ownership moved
                break;
            }
        }
        fresh.push_back(std::move(app));
    }
    free_boxart();
    apps_ = std::move(fresh);
    cur_host_ = host;
    resolve_running_app(*host);
    apps_select(sel_app_);
    app_name_lines_for_ = -1;
    for (auto &app : apps_) {
        if (app.art_state != AppEntry::ArtState::Ready) {
            queue_boxart(app);
        }
    }
}

void MoonlightUi::queue_boxart(AppEntry &app) {
    if (!cur_host_ || app.builtin != AppEntry::Builtin::None) {
        return;
    }
    auto host = cur_host_;
    auto result = std::make_shared<ArtResult>();
    std::string key = host->cache_key();
    int id = app.id;
    app.art_result = result;
    app.art_state = AppEntry::ArtState::Loading;
    app.job = Worker::instance().post(
        [host, result, key, id] {
            std::string path = boxart::cache_path(key, id);
            if (boxart::load_cached(path, result->tiled)) {
                result->ok = true;
                return;
            }
            char *data = nullptr;
            size_t size = 0;
            http_set_timeout_s(20);
            int status = gs_download_boxart(&host->server, id, &data, &size);
            http_set_timeout_s(60);
            if (status == GS_OK && data != nullptr) {
                if (boxart::decode_png((const u8 *)data, size,
                                       result->tiled)) {
                    result->ok = true;
                    boxart::ensure_cache_dir(key);
                    boxart::save_cache(path, result->tiled);
                }
            }
            free(data);
        },
        true, true); // background, droppable
}

void MoonlightUi::free_boxart() {
    for (auto &app : apps_) {
        boxart::texture_free(app.art);
        if (app.art_state == AppEntry::ArtState::Ready) {
            app.art_state = AppEntry::ArtState::Pending;
        }
    }
}

void MoonlightUi::requeue_boxart() {
    for (auto &app : apps_) {
        // Also art whose download was dropped (e.g. when a stream started)
        bool dropped = app.art_state == AppEntry::ArtState::Loading &&
                       app.job && app.job->cancelled.load();
        if (app.art_state == AppEntry::ArtState::Pending ||
            app.art_state == AppEntry::ArtState::Missing || dropped) {
            queue_boxart(app);
        }
    }
}

void MoonlightUi::leave_apps() {
    Worker::instance().drop_background();
    pending_apps_.reset();
    free_boxart();
    apps_.clear();
    cur_host_ = nullptr;
    view_ = View::Pcs;
}

bool MoonlightUi::quit_running_app(const std::shared_ptr<HostEntry> &host) {
    auto status = std::make_shared<int>(GS_FAILED);
    auto error = std::make_shared<std::string>();
    auto job = Worker::instance().post([host, status, error] {
        http_set_timeout_s(20);
        *status = gs_quit_app(&host->server);
        http_set_timeout_s(60);
        if (*status != GS_OK) {
            *error = gs_error ? gs_error : "Quit request failed";
        }
    });
    Modal m;
    m.title = "Quitting app";
    m.busy_text = host->display_name();
    if (!wait_job(job, m)) {
        return false;
    }
    if (*status != GS_OK) {
        show_error("Unable to quit the app", *error);
        return false;
    }
    host->current_game = 0;
    host->running_uuid.clear();
    host->server.currentGame = 0;
    toast("App closed");
    return true;
}

} // namespace ui
