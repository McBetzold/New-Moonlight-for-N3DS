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

#include "host_store.hpp"
#include "../system/pair_record.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

namespace hosts {

static const char *HOSTS_FILE = MOONLIGHT_3DS_PATH "/hosts.conf";

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static std::vector<Record> import_legacy() {
    std::vector<Record> out;
    for (auto entry : list_paired_addresses()) {
        trim(entry);
        if (entry.empty()) {
            continue;
        }
        Record r;
        if (!split_address(entry, r.address, r.port)) {
            continue;
        }
        out.push_back(r);
    }
    return out;
}

std::vector<Record> load() {
    if (!file_exists(HOSTS_FILE)) {
        auto imported = import_legacy();
        if (!imported.empty()) {
            save(imported);
        }
        return imported;
    }

    std::vector<Record> out;
    std::ifstream file(HOSTS_FILE);
    std::string line;
    while (std::getline(file, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        Record r;
        size_t tab1 = line.find('\t');
        if (tab1 == std::string::npos) {
            // Old style "address:port"
            if (split_address(line, r.address, r.port)) {
                out.push_back(r);
            }
            continue;
        }
        r.address = line.substr(0, tab1);
        size_t tab2 = line.find('\t', tab1 + 1);
        std::string port_str = line.substr(
            tab1 + 1,
            tab2 == std::string::npos ? std::string::npos : tab2 - tab1 - 1);
        int port = atoi(port_str.c_str());
        r.port = (port > 0 && port < 65536) ? (uint16_t)port : 47989;
        if (tab2 != std::string::npos) {
            r.name = line.substr(tab2 + 1);
            trim(r.name);
        }
        trim(r.address);
        if (!r.address.empty()) {
            out.push_back(r);
        }
    }
    return out;
}

void save(const std::vector<Record> &records) {
    mkdir(MOONLIGHT_3DS_PATH, 0777);
    FILE *fd = fopen(HOSTS_FILE, "w");
    if (!fd) {
        return;
    }
    fprintf(fd, "# address<TAB>port<TAB>name\n");
    for (const auto &r : records) {
        std::string name = r.name;
        for (auto &c : name) {
            if (c == '\t' || c == '\n' || c == '\r') {
                c = ' ';
            }
        }
        fprintf(fd, "%s\t%u\t%s\n", r.address.c_str(), (unsigned)r.port,
                name.c_str());
    }
    fclose(fd);
}

bool split_address(const std::string &input, std::string &address,
                   uint16_t &port) {
    std::string s = input;
    trim(s);
    if (s.empty()) {
        return false;
    }
    port = 47989;
    size_t colon = s.rfind(':');
    // Only treat it as a port when everything after the colon is digits
    // (keeps IPv6-looking input from being mangled).
    if (colon != std::string::npos && s.find(':') == colon) {
        std::string port_str = s.substr(colon + 1);
        bool digits = !port_str.empty();
        for (char c : port_str) {
            if (!isdigit((unsigned char)c)) {
                digits = false;
                break;
            }
        }
        if (digits) {
            int p = atoi(port_str.c_str());
            if (p > 0 && p < 65536) {
                port = (uint16_t)p;
            }
            s = s.substr(0, colon);
        }
    }
    trim(s);
    if (s.empty()) {
        return false;
    }
    address = s;
    return true;
}

std::string cache_key(const std::string &address, uint16_t port) {
    std::string key;
    for (char c : address) {
        if (isalnum((unsigned char)c)) {
            key += c;
        } else {
            key += '_';
        }
    }
    key += "_" + std::to_string(port);
    return key;
}

} // namespace hosts
