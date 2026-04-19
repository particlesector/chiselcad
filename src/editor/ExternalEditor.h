#pragma once
#include <filesystem>
#include <string>

namespace chisel::editor {

// Opens the file in VS Code (fallback: system default editor).
void openInExternalEditor(const std::filesystem::path& path);

// Opens the file and jumps to a specific line/column (1-based).
void openInExternalEditor(const std::filesystem::path& path, int line, int col);

} // namespace chisel::editor
