#include "ExternalEditor.h"
#include <cstdlib>
#include <string>

namespace chisel::editor {

void openInExternalEditor(const std::filesystem::path& path) {
    std::string p = path.string();
#if defined(_WIN32)
    if (std::system(("code \"" + p + "\"").c_str()) != 0)
        (void)std::system(("start \"\" \"" + p + "\"").c_str());
#elif defined(__APPLE__)
    if (std::system(("open -a \"Visual Studio Code\" \"" + p + "\"").c_str()) != 0)
        (void)std::system(("open \"" + p + "\"").c_str());
#else
    if (std::system(("code \"" + p + "\"").c_str()) != 0)
        (void)std::system(("xdg-open \"" + p + "\"").c_str());
#endif
}

void openInExternalEditor(const std::filesystem::path& path, int line, int col) {
    // VS Code --goto file:line:col (line and col are 1-based)
    std::string loc = path.string() + ":" + std::to_string(line) + ":" + std::to_string(col);
    if (std::system(("code --goto \"" + loc + "\"").c_str()) != 0)
        openInExternalEditor(path);
}

} // namespace chisel::editor
