#include "ExternalEditor.h"
#include <cstdlib>
#include <string>

namespace chisel::editor {

// Wrapper so callers aren't forced to check the return value.
// GCC's warn_unused_result on std::system() is not suppressible via (void) cast;
// a plain wrapper function carries no such attribute.
static int shell(const std::string& cmd) {
    return std::system(cmd.c_str());
}

void openInExternalEditor(const std::filesystem::path& path) {
    std::string p = path.string();
#if defined(_WIN32)
    if (shell("code \"" + p + "\"") != 0)
        shell("start \"\" \"" + p + "\"");
#elif defined(__APPLE__)
    if (shell("open -a \"Visual Studio Code\" \"" + p + "\"") != 0)
        shell("open \"" + p + "\"");
#else
    if (shell("code \"" + p + "\"") != 0)
        shell("xdg-open \"" + p + "\"");
#endif
}

void openInExternalEditor(const std::filesystem::path& path, int line, int col) {
    // VS Code --goto file:line:col (line and col are 1-based)
    std::string loc = path.string() + ":" + std::to_string(line) + ":" + std::to_string(col);
    if (shell("code --goto \"" + loc + "\"") != 0)
        openInExternalEditor(path);
}

} // namespace chisel::editor
