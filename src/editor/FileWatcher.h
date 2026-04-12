#pragma once
#include <filesystem>
#include <functional>
#include <string>

namespace chisel::editor {

// ---------------------------------------------------------------------------
// FileWatcher — monitors a single file for modifications.
// poll() must be called each frame; calls onChange when the file changes.
// Implementation is platform-specific (Win32 / inotify).
// ---------------------------------------------------------------------------
class FileWatcher {
public:
    using Callback = std::function<void(const std::filesystem::path&)>;

    explicit FileWatcher(std::filesystem::path path, Callback onChange);
    ~FileWatcher();

    // Non-copyable, movable
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    void poll();

private:
    std::filesystem::path       m_path;
    Callback                    m_onChange;
    std::filesystem::file_time_type m_lastWrite{};

#if defined(_WIN32)
    void* m_handle = nullptr; // HANDLE
#else
    int   m_inotifyFd = -1;
    int   m_watchFd   = -1;
#endif
};

} // namespace chisel::editor
