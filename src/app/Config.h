#pragma once
#include <filesystem>
#include <string>

namespace chisel::app {

// ---------------------------------------------------------------------------
// Config — JSON-backed user settings.  Loads from ~/.chiselcad/config.json
// (or the path passed to load()).  Missing keys use the defaults below.
// ---------------------------------------------------------------------------
struct Config {
    int    windowWidth    = 1280;
    int    windowHeight   = 800;
    float  cameraDistance = 50.0f;
    float  cameraYaw      = 0.0f;
    float  cameraPitch    = 0.4f;
    float  cameraTargetX  = 0.0f;
    float  cameraTargetY  = 0.0f;
    float  cameraTargetZ  = 0.0f;
    std::string lastFilePath;

    double globalFn       = 0.0;
    double globalFs       = 2.0;
    double globalFa       = 12.0;
    int    fontSize       = 1; // 0=small(0.85×), 1=normal(1.0×), 2=large(1.3×)

    // Analysis preferences
    bool   warnOverlappingRoots = false;

    static Config load(const std::filesystem::path& path);
    void          save(const std::filesystem::path& path) const;

    static std::filesystem::path defaultPath();
};

} // namespace chisel::app
