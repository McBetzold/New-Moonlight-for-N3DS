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

// The subnet scan uses libcurl's multi interface with CONNECT_ONLY handles
// rather than raw non-blocking sockets: curl's connect path is what every
// other request in the app already relies on, so it is known to work on the
// 3DS socket stack.

#include "discovery.hpp"

#include <3ds.h>
#include <curl/curl.h>

#include <arpa/inet.h>
#include <map>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

namespace discovery {

static const uint16_t GAMESTREAM_PORT = 47989;

static std::string addr_to_string(uint32_t host_order) {
    struct in_addr a;
    a.s_addr = htonl(host_order);
    const char *s = inet_ntoa(a);
    return s ? std::string(s) : std::string();
}

static bool local_subnet(uint32_t &my_ip, uint32_t &netmask) {
    struct in_addr ip, mask, bcast;
    memset(&ip, 0, sizeof(ip));
    memset(&mask, 0, sizeof(mask));
    memset(&bcast, 0, sizeof(bcast));
    if (SOCU_GetIPInfo(&ip, &mask, &bcast) == 0 && ip.s_addr != 0) {
        my_ip = ntohl(ip.s_addr);
        netmask = ntohl(mask.s_addr);
    } else {
        long id = gethostid();
        if (id == 0 || id == -1) {
            return false;
        }
        my_ip = ntohl((uint32_t)id);
        netmask = 0xFFFFFF00u;
    }
    // Never scan more than a /24
    if (netmask == 0 || netmask < 0xFFFFFF00u) {
        netmask = 0xFFFFFF00u;
    }
    return true;
}

std::vector<std::string> scan_local_network(int timeout_ms, int concurrency,
                                            int max_total_ms) {
    std::vector<std::string> found;

    uint32_t my_ip = 0, netmask = 0;
    if (!local_subnet(my_ip, netmask)) {
        return found;
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

    CURLM *multi = curl_multi_init();
    if (!multi) {
        return found;
    }

    std::map<CURL *, uint32_t> handles;
    size_t next = 0;
    auto add_more = [&]() {
        while ((int)handles.size() < concurrency && next < targets.size()) {
            uint32_t a = targets[next++];
            CURL *easy = curl_easy_init();
            if (!easy) {
                break;
            }
            std::string url = "http://" + addr_to_string(a) + ":" +
                              std::to_string(GAMESTREAM_PORT) + "/";
            curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
            curl_easy_setopt(easy, CURLOPT_CONNECT_ONLY, 1L);
            curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, (long)timeout_ms);
            curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, (long)timeout_ms + 250);
            curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(easy, CURLOPT_FRESH_CONNECT, 1L);
            curl_easy_setopt(easy, CURLOPT_FORBID_REUSE, 1L);
            if (curl_multi_add_handle(multi, easy) != CURLM_OK) {
                curl_easy_cleanup(easy);
                break;
            }
            handles[easy] = a;
        }
    };

    u64 start = osGetTime();
    add_more();
    while (!handles.empty()) {
        if ((int)(osGetTime() - start) > max_total_ms) {
            break;
        }
        int running = 0;
        curl_multi_perform(multi, &running);
        int numfds = 0;
        curl_multi_wait(multi, NULL, 0, 100, &numfds);

        CURLMsg *msg;
        int left = 0;
        while ((msg = curl_multi_info_read(multi, &left)) != NULL) {
            if (msg->msg != CURLMSG_DONE) {
                continue;
            }
            CURL *easy = msg->easy_handle;
            auto it = handles.find(easy);
            if (it != handles.end()) {
                if (msg->data.result == CURLE_OK) {
                    found.push_back(addr_to_string(it->second));
                }
                handles.erase(it);
            }
            curl_multi_remove_handle(multi, easy);
            curl_easy_cleanup(easy);
        }
        add_more();
    }

    for (auto &entry : handles) {
        curl_multi_remove_handle(multi, entry.first);
        curl_easy_cleanup(entry.first);
    }
    curl_multi_cleanup(multi);
    return found;
}

} // namespace discovery
