#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif
#include "Config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <spdlog/spdlog.h>

namespace chisel::app {

using json = nlohmann::json;

std::filesystem::path Config::defaultPath() {
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    std::filesystem::path base = appdata ? appdata : ".";
#else
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? home : ".";
#endif
    return base / ".chiselcad" / "config.json";
}

Config Config::load(const std::filesystem::path& path) {
    Config cfg;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return cfg;

    try {
        std::ifstream f(path);
        json j = json::parse(f);
        if (j.contains("windowWidth"))    cfg.windowWidth    = j["windowWidth"];
        if (j.contains("windowHeight"))   cfg.windowHeight   = j["windowHeight"];
        if (j.contains("cameraDistance")) cfg.cameraDistance = j["cameraDistance"];
        if (j.contains("globalFn"))       cfg.globalFn       = j["globalFn"];
        if (j.contains("globalFs"))       cfg.globalFs       = j["globalFs"];
        if (j.contains("globalFa"))       cfg.globalFa       = j["globalFa"];
    } catch (const std::exception& e) {
        spdlog::warn("Config load failed: {}", e.what());
    }
    return cfg;
}

void Config::save(const std::filesystem::path& path) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    try {
        json j;
        j["windowWidth"]    = windowWidth;
        j["windowHeight"]   = windowHeight;
        j["cameraDistance"] = cameraDistance;
        j["globalFn"]       = globalFn;
        j["globalFs"]       = globalFs;
        j["globalFa"]       = globalFa;
        std::ofstream f(path);
        f << j.dump(2);
    } catch (const std::exception& e) {
        spdlog::warn("Config save failed: {}", e.what());
    }
}

} // namespace chisel::app
