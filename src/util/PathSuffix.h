#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace chisel::util {

// True if `path`'s filename ends with `suffix` (e.g. ".stl"), case-insensitive.
// Matches by filename suffix rather than std::filesystem::path::extension()
// — the latter treats a file literally named ".stl" (leading dot, no other
// dot) as having *no* extension, per the standard's "dotfile" rule, which is
// wrong for format dispatch on a user-supplied import()/surface() path.
inline bool hasSuffixCI(const std::filesystem::path& path, std::string_view suffix) {
    std::string filename = path.filename().string();
    for (char& c : filename)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string lowerSuffix(suffix);
    for (char& c : lowerSuffix)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return filename.size() >= lowerSuffix.size() &&
           filename.compare(filename.size() - lowerSuffix.size(), lowerSuffix.size(),
                            lowerSuffix) == 0;
}

} // namespace chisel::util
