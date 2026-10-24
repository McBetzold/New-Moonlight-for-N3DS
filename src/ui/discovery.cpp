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

#include "discovery.hpp"

#include <3ds.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

namespace discovery {

static const uint16_t GAMESTREAM_PORT = 47989;

struct Pending {
    int fd;
    uint32_t addr;
    u64 started;
};

static std::string addr_to_string(uint32_t host_order) {
    struct in_addr a;
    a.s_addr = htonl(host_order);
    const char *s = inet_ntoa(a);
    return s ? std::string(s) : std::string();
}

std::vector<std::string> scan_local_network(int timeout_ms, int concurrency,
                                            int max_total_ms) {
    std::vector<std::string> found;

    struct in_addr ip, mask, bcast;
    memset(&ip, 0, sizeof(ip));
    memset(&mask, 0, sizeof(mask));
    memset(&bcast, 0, sizeof(bcast));
    if (SOCU_GetIPInfo(&ip, &mask, &bcast) != 0) {
        return found;
    }
    uint32_t my_ip = ntohl(ip.s_addr);
    uint32_t netmask = ntohl(mask.s_addr);
    if (my_ip == 0) {
        return found;
    }
    // Never scan more than a /24
    if (netmask == 0 || netmask < 0xFFFFFF00u) {
        netmask = 0xFFFFFF00u;
    }
    uint32_t base = my_ip & netmask;
    uint32_t span = (~netmask) + 1;

    std::vector<uint32_t> targets;
    for (uint32_t i = 1; i + 1 < span; i++) {
        uint32_t a = base + i;
        if (a != my_ip) {
            targets.push_back(a);
        }
    }

    std::vector<Pending> active;
    size_t next = 0;
    u64 scan_start = osGetTime();

    while (next < targets.size() || !active.empty()) {
        if ((int)(osGetTime() - scan_start) > max_total_ms) {
            break;
        }
        // Fill the window
        while ((int)active.size() < concurrency && next < targets.size()) {
            uint32_t a = targets[next];
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                break; // out of sockets: wait for the active ones first
            }
            next++;
            int flags = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_port = htons(GAMESTREAM_PORT);
            sa.sin_addr.s_addr = htonl(a);
            int r = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
            if (r == 0) {
                found.push_back(addr_to_string(a));
                close(fd);
                continue;
            }
            if (errno != EINPROGRESS && errno != EWOULDBLOCK &&
                errno != EAGAIN) {
                close(fd);
                continue;
            }
            active.push_back({fd, a, osGetTime()});
        }
        if (active.empty()) {
            if (next < targets.size()) {
                // socket() keeps failing; give the stack a moment
                svcSleepThread(50 * 1000 * 1000);
                continue;
            }
            break;
        }

        std::vector<struct pollfd> pfds;
        pfds.reserve(active.size());
        for (const auto &p : active) {
            struct pollfd pfd;
            pfd.fd = p.fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            pfds.push_back(pfd);
        }
        poll(pfds.data(), pfds.size(), 100);

        u64 now = osGetTime();
        std::vector<Pending> still;
        for (size_t i = 0; i < active.size(); i++) {
            const Pending &p = active[i];
            short re = pfds[i].revents;
            if (re & (POLLOUT | POLLERR | POLLHUP | POLLNVAL)) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (getsockopt(p.fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
                    err = errno ? errno : -1;
                }
                if (err == 0 && !(re & (POLLERR | POLLHUP | POLLNVAL))) {
                    found.push_back(addr_to_string(p.addr));
                }
                close(p.fd);
            } else if ((int)(now - p.started) > timeout_ms) {
                close(p.fd);
            } else {
                still.push_back(p);
            }
        }
        active.swap(still);
    }

    for (const auto &p : active) {
        close(p.fd);
    }
    return found;
}

} // namespace discovery
