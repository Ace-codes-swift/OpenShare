/*
 * OpenShare
 * Copyright (C) 2026 Ace Jones / ATech
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "env.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace openshare {

std::unordered_map<std::string, std::string>& Env::store() {
    static std::unordered_map<std::string, std::string> s;
    return s;
}

static std::string trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

bool Env::loadFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (!value.empty() && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        store()[key] = value;
    }
    return true;
}

void Env::loadDefaults() {
    // Prefer .env in cwd, then parent (when run from build/).
    if (!loadFile(".env")) {
        loadFile("../.env");
    }

    static const char* keys[] = {
        "OPENSHARE_HOST",
        "OPENSHARE_PORT",
        "OPENSHARE_VIDEO_WIDTH",
        "OPENSHARE_VIDEO_HEIGHT",
        "OPENSHARE_TARGET_FPS",
        "OPENSHARE_COUNTER_WINDOW",
        nullptr,
    };
    for (int i = 0; keys[i]; ++i) {
        if (const char* v = std::getenv(keys[i])) {
            store()[keys[i]] = v;
        }
    }
}

std::string Env::get(const std::string& key, const std::string& fallback) {
    auto it = store().find(key);
    if (it != store().end()) {
        return it->second;
    }
    if (const char* v = std::getenv(key.c_str())) {
        return v;
    }
    return fallback;
}

int Env::getInt(const std::string& key, int fallback) {
    const std::string v = get(key, "");
    if (v.empty()) {
        return fallback;
    }
    try {
        return std::stoi(v);
    } catch (...) {
        return fallback;
    }
}

bool Env::has(const std::string& key) {
    return !get(key, "").empty();
}

} // namespace openshare
