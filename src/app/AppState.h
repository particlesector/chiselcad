#pragma once
#include <atomic>
#include <filesystem>

namespace chisel::app {

// ---------------------------------------------------------------------------
// AppState — lightweight atomic flags shared between threads.
// The file-watcher callback sets meshDirty from its thread; the main loop
// reads it and triggers re-evaluation.
// ---------------------------------------------------------------------------
struct AppState {
    std::atomic<bool> meshDirty{false}; // true → re-evaluate .scad on next frame
    std::atomic<bool> running{true};

    std::filesystem::path scadPath;     // set once at startup, read-only after
};

} // namespace chisel::app
