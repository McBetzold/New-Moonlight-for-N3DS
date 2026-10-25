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

#include "wake_on_lan.hpp"

#include <3ds.h>
#include <arpa/inet.h>
#include <cctype>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace wol {

static bool parse_mac(const std::string &mac, unsigned char out[6]) {
    int n = 0;
    int nibbles = 0;
    unsigned char cur = 0;
    for (char ch : mac) {
        unsigned char c = (unsigned char)ch;
        if (c == ':' || c == '-' || c == '.' || isspace(c)) {
            continue;
        }
        int v;
        if (isdigit(c)) {
            v = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            v = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            v = c - 'A' + 10;
        } else {
            return false;
        }
        cur = (unsigned char)((cur << 4) | v);
        if (++nibbles == 2) {
            if (n == 6) {
                return false;
            }
            out[n++] = cur;
            nibbles = 0;
            cur = 0;
        }
    }
    return n == 6 && nibbles == 0;
}

bool valid_mac(const std::string &mac) {
    unsigned char bytes[6];
    if (!parse_mac(mac, bytes)) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (bytes[i] != 0) {
            return true;
        }
    }
    return false;
}

static bool send_to(int sock, const unsigned char *packet, size_t len,
                    in_addr_t addr, uint16_t port) {
    sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr.s_addr = addr;
    return sendto(sock, packet, len, 0, (sockaddr *)&dest, sizeof(dest)) ==
           (ssize_t)len;
}

bool send(const std::string &mac, const std::string &address) {
    unsigned char bytes[6];
    if (!parse_mac(mac, bytes)) {
        return false;
    }
    // Magic packet: 6 x 0xFF followed by the MAC 16 times
    unsigned char packet[6 + 16 * 6];
    memset(packet, 0xFF, 6);
    for (int i = 0; i < 16; i++) {
        memcpy(packet + 6 + i * 6, bytes, 6);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return false;
    }
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    bool sent = false;
    // Ports 9 (discard) and 7 (echo) are the two conventional WoL targets
    const uint16_t ports[2] = {9, 7};
    for (uint16_t port : ports) {
        sent |= send_to(sock, packet, sizeof(packet), htonl(INADDR_BROADCAST),
                        port);
    }

    // Directed packet, and the broadcast address of the host's /24 in case
    // the limited broadcast does not leave the 3DS's interface
    in_addr host_addr;
    bool have_host = inet_aton(address.c_str(), &host_addr) != 0;
    if (!have_host) {
        hostent *he = gethostbyname(address.c_str());
        if (he != NULL && he->h_addrtype == AF_INET && he->h_addr_list[0]) {
            memcpy(&host_addr, he->h_addr_list[0], sizeof(host_addr));
            have_host = true;
        }
    }
    if (have_host) {
        for (uint16_t port : ports) {
            sent |= send_to(sock, packet, sizeof(packet), host_addr.s_addr,
                            port);
            in_addr_t subnet_bcast = host_addr.s_addr | htonl(0xFF);
            sent |= send_to(sock, packet, sizeof(packet), subnet_bcast, port);
        }
    }

    close(sock);
    return sent;
}

} // namespace wol
