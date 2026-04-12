#pragma once
#include <filesystem>
#include <string>

namespace chisel::editor {

// Opens the given file in the system's default editor (or VS Code if available).
void openInExternalEditor(const std::filesystem::path& path);

} // namespace chisel::editor
