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
#pragma once

#include "../system/AtomicVar.hpp"

#include <3ds.h>
#include <deque>
#include <functional>
#include <memory>

namespace ui {

// Single background thread that runs every libgamestream / network call so
// the UI thread can keep rendering. libgamestream keeps global state (one curl
// handle, the client certificate), so all of its calls are serialized here.
// Jobs posted as "background" (box art downloads, status refreshes) only run
// when no interactive job is waiting.
class Worker {
  public:
    struct Job {
        std::function<void()> fn;
        AtomicVar<bool> done{false};
        // Set when drop_background() discarded the job before it ran; done
        // is set too, so anything waiting on it wakes up
        AtomicVar<bool> cancelled{false};
        bool droppable = false;
        bool is_done() { return done.load(); }
    };

    static Worker &instance();

    // droppable: may be discarded by drop_background() (box art). Status
    // checks and app-list loads must not be, or whoever waits on them would
    // wait forever.
    std::shared_ptr<Job> post(std::function<void()> fn,
                              bool background = false, bool droppable = false);
    // Discards queued (not yet running) droppable background jobs and marks
    // them done + cancelled.
    void drop_background();
    // True while a job is running or queued.
    bool busy();
    void shutdown();

  private:
    Worker();
    ~Worker();
    static void thread_main(void *arg);
    void run();

    LightLock lock_;
    LightSemaphore sem_;
    std::deque<std::shared_ptr<Job>> high_;
    std::deque<std::shared_ptr<Job>> low_;
    std::shared_ptr<Job> running_;
    Thread thread_ = nullptr;
    bool quit_ = false;
};

} // namespace ui
