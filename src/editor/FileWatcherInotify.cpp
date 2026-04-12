#if !defined(_WIN32)
#include "FileWatcher.h"
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <system_error>

namespace chisel::editor {

FileWatcher::FileWatcher(std::filesystem::path path, Callback onChange)
    : m_path(std::move(path)), m_onChange(std::move(onChange))
{
    std::error_code ec;
    m_lastWrite = std::filesystem::last_write_time(m_path, ec);

    m_inotifyFd = inotify_init1(IN_NONBLOCK);
    if (m_inotifyFd >= 0) {
        m_watchFd = inotify_add_watch(m_inotifyFd, m_path.parent_path().c_str(),
                                       IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    }
}

FileWatcher::~FileWatcher() {
    if (m_watchFd >= 0)   { inotify_rm_watch(m_inotifyFd, m_watchFd); m_watchFd   = -1; }
    if (m_inotifyFd >= 0) { close(m_inotifyFd);                        m_inotifyFd = -1; }
}

void FileWatcher::poll() {
    if (m_inotifyFd < 0) return;

    alignas(inotify_event) char buf[4096];
    ssize_t len = read(m_inotifyFd, buf, sizeof(buf));
    if (len <= 0) return;

    // Any event in the directory → check if our file changed
    std::error_code ec;
    auto t = std::filesystem::last_write_time(m_path, ec);
    if (!ec && t != m_lastWrite) {
        m_lastWrite = t;
        m_onChange(m_path);
    }
}

} // namespace chisel::editor
#endif
