#pragma once
#include <filesystem>
#include <string>

namespace chisel::util {

// Builds a std::filesystem::path from a std::string that is known to hold
// UTF-8 bytes (as chiselcad's lexer/parser always produce for path-like
// string literals). Constructing std::filesystem::path directly from a
// std::string interprets the bytes using the implementation's native narrow
// encoding, which on Windows is the process ANSI code page rather than
// UTF-8 — routing through std::u8string forces UTF-8 interpretation on every
// platform, matching Linux's already-correct passthrough behavior.
inline std::filesystem::path utf8ToPath(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

} // namespace chisel::util
