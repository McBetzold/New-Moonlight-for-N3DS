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

#include <errors.h>
#include <http.h>

#include <algorithm>
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
    std::string desc = server.isNvidiaSoftware ? "GeForce Experience" : "Sunshine";
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
        hosts_.push_back(h);
    }
}

MoonlightUi::~MoonlightUi() {
    Worker::instance().drop_background();
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
}

// ---- frame plumbing -------------------------------------------------------

bool MoonlightUi::poll() {
    if (!aptMainLoop()) {
        quit_ = true;
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
            apply_probe(*it);
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
    draw_top();
    if (modal) {
        draw_modal_top(*modal);
    }
    draw_toast();

    scene(Screen::Bottom, color::bg);
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
    rect(0, HEADER_H, TOP_W, 1, color::border);
    float x = 12.0f;
    if (logo) {
        icon::moon(x + 9, HEADER_H / 2, 9, color::accent, color::header);
        x += 26.0f;
    }
    text(title, x, 5.0f, 0.6f, color::text);
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
    rect(0, TOOLBAR_H - 1, BOT_W, 1, color::border);
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
            rect(x, 0, bw, TOOLBAR_H - 1, color::card_hi);
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
    rrect(x, y, w, h, 6, color::accent_dim);
    text(toast_text_, TOP_W / 2.0f, y + 5.0f, 0.45f, color::text,
         C2D_AlignCenter);
}

void MoonlightUi::modal_button_rect(const Modal &m, int index, float &x,
                                    float &y, float &w, float &h) const {
    int n = (int)m.buttons.size();
    if (n <= 2) {
        float gap = 12.0f;
        w = (BOT_W - 40.0f - (n - 1) * gap) / (float)n;
        h = 44.0f;
        x = 20.0f + index * (w + gap);
        y = m.busy ? 170.0f : 104.0f;
    } else {
        // Stack vertically and shrink the buttons so the whole list always
        // fits on the 240px screen (six items overflowed at a fixed 36px).
        const float margin = 12.0f;
        const float avail = BOT_H - 2.0f * margin;
        float gap = n > 4 ? 6.0f : 8.0f;
        h = std::min(36.0f, (avail - (n - 1) * gap) / (float)n);
        w = 232.0f;
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
    rect(x + 8, y, w - 16, 3, color::accent);
    float cy = y + 12.0f;
    text(ellipsize(m.title, 0.55f, w - 32.0f), x + 16.0f, cy, 0.55f,
         color::accent_text);
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
        float bw = text_width(m.big_text, scale) + 40.0f;
        float bh = line_height(scale) + 8.0f;
        float by = std::min(cy, y + h - bh - 12.0f);
        rrect((TOP_W - bw) / 2.0f, by, bw, bh, 8, color::card);
        text(m.big_text, TOP_W / 2.0f, by + 4.0f, scale, color::text,
             C2D_AlignCenter);
    }
}

void MoonlightUi::draw_modal_bottom(const Modal &m) {
    rect(0, 0, BOT_W, BOT_H, color::overlay);
    if (m.busy) {
        icon::spinner(BOT_W / 2.0f, 84.0f, 18.0f, color::accent, now_);
        if (!m.busy_text.empty()) {
            auto lines = wrap(m.busy_text, 0.42f, BOT_W - 48.0f);
            if (lines.size() > 2) {
                lines.resize(2);
            }
            text_block(lines, BOT_W / 2.0f, 116.0f, 0.42f, color::text_dim,
                       C2D_AlignCenter);
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
        if (sel) {
            rrect(x - 2, y - 2, w + 4, h + 4, 8, color::accent_text);
        }
        rrect(x, y, w, h, 6, fill);
        text(ellipsize(m.buttons[i], 0.48f, w - 16.0f), x + w / 2.0f,
             y + (h - line_height(0.48f)) / 2.0f, 0.48f, color::text,
             C2D_AlignCenter);
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
    bool vertical = n > 2;
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
            if (in_.pressed(KEY_B) && !m.busy) {
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

bool MoonlightUi::wait_job(const std::shared_ptr<Worker::Job> &job, Modal &m) {
    m.busy = true;
    int r = show_modal(m, [&job] { return job->is_done(); });
    if (r == -3) {
        // App is closing: still let the job finish so libgamestream state is
        // consistent before teardown.
        while (!job->is_done()) {
            svcSleepThread(5 * 1000 * 1000);
        }
        return false;
    }
    return job->is_done();
}

bool MoonlightUi::prompt_text(const std::string &hint,
                              const std::string &initial, std::string &out,
                              size_t max_len) {
    std::vector<char> buf(max_len + 1, 0);
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, (int)max_len);
    swkbdSetHintText(&swkbd, hint.c_str());
    if (!initial.empty()) {
        swkbdSetInitialText(&swkbd, initial.c_str());
    }
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);
    SwkbdButton pressed = swkbdInputText(&swkbd, buf.data(), buf.size());
    input_reset();
    if (pressed != SWKBD_BUTTON_RIGHT) {
        return false;
    }
    out = buf.data();
    trim(out);
    return !out.empty();
}

bool MoonlightUi::prompt_number(const std::string &hint, int initial,
                                int &out) {
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
    if (value <= 0) {
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
            connect_to(hosts_[sel_pc_]);
            return;
        }
        if (in.pressed(KEY_X)) {
            pc_options();
            return;
        }
    }

    if (in.pressed(KEY_B)) {
        if (confirm("Exit Moonlight?",
                    "Close Moonlight and return to the HOME Menu?", "Exit")) {
            quit_ = true;
        }
    }
}

void MoonlightUi::pcs_draw_top() {
    draw_header("Moonlight", "Computers", true);

    if (hosts_.empty()) {
        text("No PCs added yet", TOP_W / 2.0f, 76.0f, 0.6f, color::text,
             C2D_AlignCenter);
        auto lines =
            wrap("Tap  Add PC  on the bottom screen and enter the IP address "
                 "of the PC running Sunshine or GeForce Experience.",
                 0.42f, TOP_W - 80.0f);
        text_block(lines, TOP_W / 2.0f, 104.0f, 0.42f, color::text_dim,
                   C2D_AlignCenter);
        text("Automatic PC discovery is not available on the 3DS.",
             TOP_W / 2.0f, 160.0f, 0.38f, color::text_faint, C2D_AlignCenter);
        draw_hints({{"A", "Add PC"}, {"START", "Settings"}, {"B", "Exit"}});
        return;
    }

    const HostEntry &h = *hosts_[sel_pc_];
    const float cx = 20.0f, cy = 42.0f, cw = TOP_W - 40.0f, ch = 168.0f;
    rrect(cx, cy, cw, ch, 8, color::surface);

    // Big icon with status badge
    float ix = cx + 58.0f, iy = cy + 66.0f;
    icon::desktop(ix, iy, 72, h.online || !h.status_known ? color::text
                                                           : color::text_faint);
    if (h.probing) {
        icon::spinner(ix, iy - 6, 14, color::accent, now_);
    } else if (h.status_known && !h.online) {
        icon::warning(ix + 26, iy + 14, 30, color::warn, color::black);
    } else if (h.status_known && !h.paired) {
        icon::lock(ix + 26, iy + 14, 30, color::text_dim, color::surface);
    }

    float tx = cx + 120.0f;
    float ty = cy + 16.0f;
    text(ellipsize(h.display_name(), 0.62f, cw - 136.0f), tx, ty, 0.62f,
         color::text);
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
        if (sel) {
            rrect(x - 3, y - 3, PC_W + 6, PC_H + 6, 9, color::accent);
        }
        u32 card = sel ? color::card_hi : color::card;
        rrect(x, y, PC_W, PC_H, 7, card);

        float icx = x + PC_W / 2.0f, icy = y + 32.0f;
        icon::desktop(icx, icy, 38, h.online || !h.status_known
                                        ? color::text
                                        : color::text_faint);
        if (h.probing) {
            icon::spinner(icx, icy - 3, 8, color::accent, now_);
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
        icon::desktop(BOT_W / 2.0f, 110.0f, 56, color::card_hi);
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
        h.server = r.server;
        h.server_valid = true;
        h.server_desc = r.desc;
        h.last_error.clear();
        if (!r.hostname.empty() && r.hostname != h.name) {
            h.name = r.hostname;
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
    if (!probe_host_blocking(host, "Connecting to " + host->address_text())) {
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
    enum { ACT_CONNECT, ACT_PAIR, ACT_UNPAIR, ACT_RENAME, ACT_REFRESH, ACT_DELETE };
    items.push_back("Connect");
    actions.push_back(ACT_CONNECT);
    if (host->status_known && host->online) {
        items.push_back(host->paired ? "Unpair" : "Pair");
        actions.push_back(host->paired ? ACT_UNPAIR : ACT_PAIR);
    }
    items.push_back("Rename");
    actions.push_back(ACT_RENAME);
    items.push_back("Refresh status");
    actions.push_back(ACT_REFRESH);
    items.push_back("Delete PC");
    actions.push_back(ACT_DELETE);

    int choice = menu(host->display_name(), items);
    if (choice < 0) {
        return;
    }
    switch (actions[choice]) {
    case ACT_CONNECT:
        connect_to(host);
        break;
    case ACT_PAIR:
        pair_host(host);
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
    if (prompt_text("Name for this PC", host->display_name(), name, 40)) {
        host->name = name;
        save_hosts();
    }
}

void MoonlightUi::delete_host(const std::shared_ptr<HostEntry> &host) {
    if (!confirm("Delete " + host->display_name() + "?",
                 "The PC is removed from this list. Pairing on the host PC "
                 "is not affected.",
                 "Delete", "Cancel", true)) {
        return;
    }
    hosts_.erase(std::remove(hosts_.begin(), hosts_.end(), host), hosts_.end());
    remove_pair_address(host->address, host->port);
    save_hosts();
    pcs_select(sel_pc_);
    toast("Deleted " + host->display_name());
}

void MoonlightUi::pair_host(const std::shared_ptr<HostEntry> &host) {
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

    char pin_buf[8];
    snprintf(pin_buf, sizeof(pin_buf), "%04u", (unsigned)(rand() % 10000));
    std::string pin = pin_buf;

    auto status = std::make_shared<int>(GS_FAILED);
    auto error = std::make_shared<std::string>();
    auto job = Worker::instance().post([host, pin, status, error] {
        // Pairing waits for the user to type the PIN on the PC
        http_set_timeout_s(3 * 60);
        http_set_connect_timeout_s(10);
        std::string pin_copy = pin;
        *status = gs_pair(&host->server, &pin_copy[0]);
        http_set_timeout_s(60);
        if (*status != GS_OK) {
            *error = gs_error ? gs_error : "Pairing failed";
        }
    });

    Modal m;
    m.title = "Pairing with " + host->display_name();
    m.message =
        "Enter this PIN on the host PC. With Sunshine, open the web UI and "
        "use the PIN page. This dialog closes when pairing completes.";
    m.big_text = pin;
    m.busy_text = "Waiting for the PIN to be entered on the PC";
    if (!wait_job(job, m)) {
        return;
    }

    if (*status == GS_OK) {
        host->paired = true;
        host->server.paired = true;
        add_pair_address(host->address, host->port);
        toast("Paired with " + host->display_name());
    } else {
        show_error("Pairing failed", *error);
    }
}

void MoonlightUi::unpair_host(const std::shared_ptr<HostEntry> &host) {
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
        text("No apps found", TOP_W / 2.0f, 90.0f, 0.55f, color::text,
             C2D_AlignCenter);
        auto lines = wrap("Add apps in the Sunshine web UI under "
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
    rrect(cx, cy, cw, ch, 8, color::surface);

    // Box art at native 96x128
    float ax = cx + 18.0f, ay = cy + 20.0f;
    rrect(ax - 3, ay - 3, boxart::IMG_W + 6, boxart::IMG_H + 6, 5,
          running ? color::ok : color::card_hi);
    if (app.art_state == AppEntry::ArtState::Ready) {
        boxart::draw(app.art, ax, ay, 1.0f);
    } else {
        rect(ax, ay, boxart::IMG_W, boxart::IMG_H, color::card);
        if (app.art_state == AppEntry::ArtState::Loading) {
            icon::spinner(ax + boxart::IMG_W / 2.0f, ay + boxart::IMG_H / 2.0f,
                          12, color::accent, now_);
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
    } else if (cur_host_ && cur_host_->current_game) {
        pill(tx, ty, "Another app is running", 0.38f, color::card_hi,
             color::text_dim);
    } else {
        pill(tx, ty, "Ready to stream", 0.38f, color::card_hi, color::text_dim);
    }
    ty += line_height(0.38f) + 16.0f;

    std::string res = std::to_string(config_->stream.width) + "x" +
                      std::to_string(config_->stream.height) + "  " +
                      std::to_string(config_->stream.fps) + " fps  " +
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
        if (sel) {
            rrect(ax - 3, ay - 3, art_w + 6, art_h + 6, 5, color::accent);
        } else if (running) {
            rrect(ax - 2, ay - 2, art_w + 4, art_h + 4, 4, color::ok);
        }
        if (app.art_state == AppEntry::ArtState::Ready) {
            boxart::draw(app.art, ax, ay, ART_SCALE);
        } else {
            rect(ax, ay, art_w, art_h, sel ? color::card_hi : color::card);
            if (app.art_state == AppEntry::ArtState::Loading) {
                icon::spinner(ax + art_w / 2.0f, ay + art_h / 2.0f, 8,
                              color::accent, now_);
            } else {
                icon::gamepad(ax + art_w / 2.0f, ay + art_h / 2.0f, 36,
                              sel ? color::card : color::card_hi,
                              color::text_faint);
            }
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
            icon::spinner(BOT_W / 2.0f, 120.0f, 16, color::accent, now_);
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
            while (list != NULL) {
                PAPP_LIST next = list->next;
                result->apps.push_back(
                    {list->id, list->name ? std::string(list->name) : ""});
                free(list->name);
                free(list);
                list = next;
            }
            std::sort(result->apps.begin(), result->apps.end(),
                      [](const std::pair<int, std::string> &a,
                         const std::pair<int, std::string> &b) {
                          return strcasecmp(a.second.c_str(),
                                            b.second.c_str()) < 0;
                      });
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
    for (const auto &entry : result.apps) {
        AppEntry app;
        app.id = entry.first;
        app.name = entry.second;
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
    apps_select(sel_app_);
    app_name_lines_for_ = -1;
    for (auto &app : apps_) {
        if (app.art_state != AppEntry::ArtState::Ready) {
            queue_boxart(app);
        }
    }
}

void MoonlightUi::queue_boxart(AppEntry &app) {
    if (!cur_host_) {
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
        true);
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
        if (app.art_state == AppEntry::ArtState::Pending ||
            app.art_state == AppEntry::ArtState::Missing) {
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
    host->server.currentGame = 0;
    toast("App closed");
    return true;
}

} // namespace ui
