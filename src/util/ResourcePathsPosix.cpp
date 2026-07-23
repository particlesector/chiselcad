#if defined(__linux__)
#include "util/ResourcePaths.h"

#include <climits>
#include <unistd.h>

namespace chisel::util {

std::filesystem::path executableDir() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        return std::filesystem::current_path();
    }
    buf[len] = '\0';
    return std::filesystem::path(buf).parent_path();
}

} // namespace chisel::util
#endif
