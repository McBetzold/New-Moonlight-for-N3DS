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

// Launcher UI modelled on the Moonlight PC/Android clients: a grid of host
// PCs with online/paired state, a box-art grid of apps per host, a grouped
// settings page and dialog-driven pairing. The touch screen carries the grid
// and the toolbar (like the Android app); the top screen shows the selected
// item in detail along with button hints.
#pragma once

#include "../config.hpp"
#include "boxart.hpp"
#include "host_store.hpp"
#include "ui_gfx.hpp"
#include "ui_input.hpp"
#include "worker.hpp"

#include <client.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ui {

struct HostEntry {
    // Never modified after creation: SERVER_DATA.serverInfo.address points
    // into this string.
    std::string address;
    uint16_t port = 47989;
    std::string name;
    std::string mac; // for Wake-on-LAN, learned from the host while paired

    bool status_known = false;
    bool online = false;
    bool paired = false;
    bool probing = false;
    // App ID running on the host; 0 = none, -1 = something is running but
    // its ID is not known yet (Apollo with Input Only Mode only reports the
    // running app's UUID; see MoonlightUi::resolve_running_app)
    int current_game = 0;
    std::string running_uuid; // Apollo: UUID of the running app, or ""
    std::string server_desc;
    std::string last_error;
    SERVER_DATA server{};
    bool server_valid = false;

    std::string display_name() const { return name.empty() ? address : name; }
    std::string address_text() const {
        return port == 47989 ? address : address + ":" + std::to_string(port);
    }
    std::string cache_key() const { return hosts::cache_key(address, port); }
};

struct ProbeResult {
    bool ok = false;
    bool paired = false;
    int current_game = 0;
    std::string running_uuid;
    std::string hostname;
    std::string desc;
    std::string error;
    SERVER_DATA server{};
};

struct PendingProbe {
    std::shared_ptr<HostEntry> host;
    std::shared_ptr<Worker::Job> job;
    std::shared_ptr<ProbeResult> result;
};

struct AppInfo {
    int id = 0;
    std::string name;
    std::string uuid; // Apollo only
    int idx = -1;     // Apollo's own ordering, -1 when absent
};

struct AppsResult {
    int status = 0;
    std::string error;
    std::vector<AppInfo> apps;
    // Apollo answered with its "Permission Denied" placeholder: this client
    // is not allowed to list apps.
    bool denied = false;
};

struct PendingApps {
    std::shared_ptr<HostEntry> host;
    std::shared_ptr<Worker::Job> job;
    std::shared_ptr<AppsResult> result;
};

struct ArtResult {
    std::vector<u8> tiled;
    bool ok = false;
};

struct AppEntry {
    int id = 0;
    std::string name;
    std::string uuid;        // Apollo only, "" otherwise
    bool input_only = false; // Apollo's "Remote Input" pseudo-app (no video)
    std::string tile_name;   // ellipsized for the grid tile
    enum class ArtState { Pending, Loading, Ready, Missing };
    ArtState art_state = ArtState::Pending;
    boxart::Texture art;
    std::shared_ptr<Worker::Job> job;
    std::shared_ptr<ArtResult> art_result;
};

// How a stream ended, for the launcher to decide between reconnecting,
// showing an error and going back to the app grid.
struct StreamOutcome {
    bool connected = false;      // LiStartConnection succeeded
    bool lost = false;           // ended by an error, not by the user
    bool home_requested = false; // the user pressed HOME to leave
    std::string message;         // error text for the user
};

struct Modal {
    std::string title;
    std::string message;  // wrapped on the top screen
    std::string big_text; // e.g. the pairing PIN
    std::vector<std::string> buttons;
    // Optional per-button second line (e.g. an IP address) and a small
    // status badge drawn at the right edge; parallel to `buttons`, empty
    // strings are skipped.
    std::vector<std::string> subtitles;
    std::vector<std::string> badges;
    bool list = false; // always stack the buttons vertically
    int selected = 0;
    bool busy = false; // show a spinner
    std::string busy_text;
    int danger_index = -1; // button drawn in red
};

struct ToolButton {
    enum class Icon { Add, Refresh, Options, Settings, Back, Stop, Exit };
    Icon icon;
    std::string label;
    bool enabled = true;
};

// One page of an in-app guide. The body is split into paragraphs at '\n';
// a paragraph starting with "* " is drawn as a bullet point.
struct GuidePage {
    const char *title;
    const char *body;
};

class MoonlightUi {
  public:
    explicit MoonlightUi(PCONFIGURATION config);
    ~MoonlightUi();

    // Runs until the user exits or the system closes the app.
    void run();

  private:
    enum class View { Pcs, Apps, Settings };

    struct SettingRow {
        // Link: opens something (e.g. a guide) when pressed or tapped
        enum Type { Header, Toggle, Choice, Slider, Number, Link };
        Type type = Header;
        std::string label;
        std::string desc;
        // Optional dynamic description (e.g. per selected preset)
        std::function<std::string()> desc_fn;
        std::function<std::string()> value;
        std::function<void(int)> adjust; // -1 / +1 (left / right)
        std::function<void()> activate;  // A button or tap
        std::function<bool()> is_on;     // toggles
        std::function<float()> fraction; // sliders, 0..1
        std::function<void(float)> set_fraction;
    };

    // ---- frame plumbing
    bool poll();
    void pump();
    void render(const Modal *modal);
    void draw_top();
    void draw_bottom();
    void draw_header(const std::string &title, const std::string &subtitle,
                     bool logo);
    void draw_hints(
        const std::vector<std::pair<const char *, std::string>> &hints);
    void draw_toolbar(const std::vector<ToolButton> &buttons);
    int toolbar_hit(const Input &in, int count) const;
    void draw_toast();
    void toast(const std::string &message);
    void draw_modal_top(const Modal &m);
    void draw_modal_bottom(const Modal &m);
    void modal_button_rect(const Modal &m, int index, float &x, float &y,
                           float &w, float &h) const;

    // ---- modals and prompts (nested render loops)
    int show_modal(Modal &m, const std::function<bool()> &finished = nullptr);
    void show_error(const std::string &title, const std::string &message);
    bool confirm(const std::string &title, const std::string &message,
                 const std::string &yes, const std::string &no = "Cancel",
                 bool danger = false);
    int menu(const std::string &title, const std::vector<std::string> &items);
    bool wait_job(const std::shared_ptr<Worker::Job> &job, Modal &m);
    bool prompt_text(const std::string &hint, const std::string &initial,
                     std::string &out, size_t max_len = 60);
    bool prompt_number(const std::string &hint, int initial, int &out);

    // ---- Computers view
    void pcs_update(const Input &in);
    void pcs_draw_top();
    void pcs_draw_bottom();
    void pcs_select(int index);
    int pcs_hit(float tx, float ty) const;
    std::vector<ToolButton> pcs_toolbar() const;
    void add_pc();
    void add_pc_manual();
    void discover_pcs();
    // Probes a freshly created entry, offers "add anyway" when offline, adds
    // it to the list and offers pairing.
    void add_host_entry(const std::shared_ptr<HostEntry> &host);
    void pc_options();
    void connect_to(const std::shared_ptr<HostEntry> &host);
    // one_time_pin: Apollo's OTP pairing, where the PIN and a passphrase
    // generated in the host's web UI are typed on the 3DS instead.
    void pair_host(const std::shared_ptr<HostEntry> &host,
                   bool one_time_pin = false);
    // Explains missing Apollo permissions; "" when nothing is missing.
    static std::string permission_warning(const HostEntry &host);
    void unpair_host(const std::shared_ptr<HostEntry> &host);
    void delete_host(const std::shared_ptr<HostEntry> &host);
    void rename_host(const std::shared_ptr<HostEntry> &host);
    void refresh_all_hosts(bool interactive);
    void probe_host(const std::shared_ptr<HostEntry> &host, bool background);
    bool probe_host_blocking(const std::shared_ptr<HostEntry> &host,
                             const std::string &title);
    void apply_probe(PendingProbe &probe);
    void save_hosts();

    // ---- Apps view
    void apps_update(const Input &in);
    void apps_draw_top();
    void apps_draw_bottom();
    void apps_select(int index);
    int apps_hit(float tx, float ty) const;
    std::vector<ToolButton> apps_toolbar() const;
    bool load_apps(const std::shared_ptr<HostEntry> &host, bool interactive);
    void apply_apps(const std::shared_ptr<HostEntry> &host,
                    const AppsResult &result);
    void queue_boxart(AppEntry &app);
    void free_boxart();
    void requeue_boxart();
    void leave_apps();
    std::string running_app_name() const;
    // Turns the host's running-app UUID into an app ID using the loaded app
    // list (Apollo's Input Only Mode reports currentgame 0 while an app runs)
    void resolve_running_app(HostEntry &host);
    bool quit_running_app(const std::shared_ptr<HostEntry> &host);

    // ---- stream launch (moonlight_ui_stream.cpp)
    void launch_app(int index);
    StreamOutcome run_stream(const std::shared_ptr<HostEntry> &host,
                             const AppEntry &app);
    void after_stream(const std::shared_ptr<HostEntry> &host);
    // Opens the host's app list and launches the app it reports as running.
    void resume_running(const std::shared_ptr<HostEntry> &host);
    // "Resume on start": after the first status check, resume a running app.
    void maybe_resume_on_start();

    // ---- Wake-on-LAN
    void wake_host(const std::shared_ptr<HostEntry> &host);

    // ---- Settings view (moonlight_ui_settings.cpp)
    void settings_enter();
    void settings_leave();
    void settings_build();
    void settings_update(const Input &in);
    void settings_draw_top();
    void settings_draw_bottom();
    void settings_select(int index);
    float settings_row_height(const SettingRow &row) const;
    float settings_content_height() const;
    int settings_hit(float tx, float ty, float &row_y) const;
    std::string resolution_warning() const;
    void settings_edit_resolution();

    // ---- In-app guides (moonlight_ui_guide.cpp); nested render loop
    void show_guide(const std::string &title, const GuidePage *pages,
                    int count);
    void show_stereo_guide();
    void show_wol_guide();

    PCONFIGURATION config_;
    View view_ = View::Pcs;
    bool quit_ = false;
    Input in_;
    u64 now_ = 0;

    std::string toast_text_;
    u64 toast_until_ = 0;

    // hosts
    std::vector<std::shared_ptr<HostEntry>> hosts_;
    int sel_pc_ = 0;
    float pc_scroll_ = 0;
    std::vector<PendingProbe> probes_;

    // apps
    std::shared_ptr<HostEntry> cur_host_;
    std::vector<AppEntry> apps_;
    int sel_app_ = 0;
    float app_scroll_ = 0;
    std::unique_ptr<PendingApps> pending_apps_;
    std::vector<std::string> app_name_lines_;
    int app_name_lines_for_ = -1;
    bool apps_denied_ = false; // Apollo refused to list apps

    // settings
    std::vector<SettingRow> rows_;
    int sel_row_ = 0;
    float settings_scroll_ = 0;
    std::vector<std::string> desc_lines_;
    int desc_lines_for_ = -1;
    std::string desc_cache_text_;
};

} // namespace ui
