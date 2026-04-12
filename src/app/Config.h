#pragma once
#include <filesystem>
#include <string>

namespace chisel::app {

// ---------------------------------------------------------------------------
// Config — JSON-backed user settings.  Loads from ~/.chiselcad/config.json
// (or the path passed to load()).  Missing keys use the defaults below.
// ---------------------------------------------------------------------------
struct Config {
    int    windowWidth  = 1280;
    int    windowHeight = 800;
    float  cameraDistance = 50.0f;
    double globalFn     = 0.0;
    double globalFs     = 2.0;
    double globalFa     = 12.0;

    static Config load(const std::filesystem::path& path);
    void          save(const std::filesystem::path& path) const;

    static std::filesystem::path defaultPath();
};

} // namespace chisel::app
