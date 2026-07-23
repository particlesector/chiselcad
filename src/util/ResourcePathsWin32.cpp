#if defined(_WIN32)
#include "util/ResourcePaths.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace chisel::util {

std::filesystem::path executableDir() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(buf, buf + len).parent_path();
}

} // namespace chisel::util
#endif
