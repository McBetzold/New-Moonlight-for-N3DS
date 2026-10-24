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

#include "worker.hpp"

namespace ui {

// LiStartConnection, curl and OpenSSL all run on this thread, so give it a
// generous stack.
static const size_t WORKER_STACK_SIZE = 0x60000;

Worker &Worker::instance() {
    static Worker worker;
    return worker;
}

Worker::Worker() {
    LightLock_Init(&lock_);
    LightSemaphore_Init(&sem_, 0, 0x7FFF);

    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    // Slightly below the UI thread so rendering always wins; the UI thread
    // sleeps on VBlank every frame which leaves plenty of time for jobs.
    if (priority < 0x3F) {
        priority++;
    }
    thread_ = threadCreate(thread_main, this, WORKER_STACK_SIZE, priority, -1,
                           false);
}

Worker::~Worker() { shutdown(); }

void Worker::thread_main(void *arg) { static_cast<Worker *>(arg)->run(); }

void Worker::run() {
    while (true) {
        LightSemaphore_Acquire(&sem_, 1);

        std::shared_ptr<Job> job;
        LightLock_Lock(&lock_);
        if (quit_) {
            LightLock_Unlock(&lock_);
            break;
        }
        if (!high_.empty()) {
            job = high_.front();
            high_.pop_front();
        } else if (!low_.empty()) {
            job = low_.front();
            low_.pop_front();
        }
        running_ = job;
        LightLock_Unlock(&lock_);

        if (!job) {
            continue;
        }
        job->fn();

        LightLock_Lock(&lock_);
        running_ = nullptr;
        LightLock_Unlock(&lock_);
        job->done.store(true);
    }
}

std::shared_ptr<Worker::Job> Worker::post(std::function<void()> fn,
                                          bool background) {
    auto job = std::make_shared<Job>();
    job->fn = std::move(fn);
    LightLock_Lock(&lock_);
    if (background) {
        low_.push_back(job);
    } else {
        high_.push_back(job);
    }
    LightLock_Unlock(&lock_);
    LightSemaphore_Release(&sem_, 1);
    return job;
}

void Worker::drop_background() {
    LightLock_Lock(&lock_);
    low_.clear();
    LightLock_Unlock(&lock_);
}

bool Worker::busy() {
    LightLock_Lock(&lock_);
    bool b = running_ != nullptr || !high_.empty() || !low_.empty();
    LightLock_Unlock(&lock_);
    return b;
}

void Worker::shutdown() {
    if (!thread_) {
        return;
    }
    LightLock_Lock(&lock_);
    quit_ = true;
    LightLock_Unlock(&lock_);
    LightSemaphore_Release(&sem_, 1);
    threadJoin(thread_, U64_MAX);
    threadFree(thread_);
    thread_ = nullptr;
}

} // namespace ui
