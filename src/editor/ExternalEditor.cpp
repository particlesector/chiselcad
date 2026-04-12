#include "ExternalEditor.h"
#include <cstdlib>
#include <string>

namespace chisel::editor {

void openInExternalEditor(const std::filesystem::path& path) {
    std::string cmd;
#if defined(_WIN32)
    cmd = "code \"" + path.string() + "\"";
    if (std::system(cmd.c_str()) != 0)
        (void)std::system(("start \"\" \"" + path.string() + "\"").c_str());
#elif defined(__APPLE__)
    cmd = "open -a \"Visual Studio Code\" \"" + path.string() + "\"";
    if (std::system(cmd.c_str()) != 0)
        (void)std::system(("open \"" + path.string() + "\"").c_str());
#else
    cmd = "code \"" + path.string() + "\"";
    if (std::system(cmd.c_str()) != 0)
        (void)std::system(("xdg-open \"" + path.string() + "\"").c_str());
#endif
}

} // namespace chisel::editor
