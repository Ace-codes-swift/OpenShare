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
