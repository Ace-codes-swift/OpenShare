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

#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace openshare {

class Env {
public:
    // Load key=value pairs from path. Later entries override earlier ones.
    // Also merges process environment (env vars win over file).
    static bool loadFile(const std::string& path);
    static void loadDefaults();

    static std::string get(const std::string& key, const std::string& fallback = "");
    static int getInt(const std::string& key, int fallback);
    static bool has(const std::string& key);

private:
    static std::unordered_map<std::string, std::string>& store();
};

} // namespace openshare
