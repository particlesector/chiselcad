#if defined(_WIN32)
#include "FileWatcher.h"
#include <system_error>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace chisel::editor {

FileWatcher::FileWatcher(std::filesystem::path path, Callback onChange)
    : m_path(std::move(path)), m_onChange(std::move(onChange))
{
    std::error_code ec;
    m_lastWrite = std::filesystem::last_write_time(m_path, ec);

    // Open a handle to the directory containing the file for change notification
    std::filesystem::path dir = m_path.parent_path();
    m_handle = FindFirstChangeNotificationW(
        dir.wstring().c_str(),
        FALSE,
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
}

FileWatcher::~FileWatcher() {
    if (m_handle && m_handle != INVALID_HANDLE_VALUE) {
        FindCloseChangeNotification(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
}

void FileWatcher::poll() {
    if (!m_handle || m_handle == INVALID_HANDLE_VALUE) return;

    DWORD result = WaitForSingleObject(static_cast<HANDLE>(m_handle), 0);
    if (result == WAIT_OBJECT_0) {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(m_path, ec);
        if (!ec && t != m_lastWrite) {
            m_lastWrite = t;
            m_onChange(m_path);
        }
        FindNextChangeNotification(static_cast<HANDLE>(m_handle));
    }
}

} // namespace chisel::editor
#endif
