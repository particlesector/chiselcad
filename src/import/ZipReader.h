#pragma once
#include <filesystem>
#include <string>

namespace chisel::io {

struct ZipExtractResult {
    std::string bytes;
    std::string error; // empty = success
};

// Reads one entry's uncompressed bytes out of a ZIP archive — STORED or
// DEFLATE compression only (the two methods every real-world 3MF writer
// uses in practice). Not a general zip library: no streaming/write support,
// no ZIP64 (files >4GB or >65535 entries — 3MF files never approach either
// limit), no encryption, no split/spanned archives.
//
// `entrySuffix` matches the first central-directory entry whose filename
// ends with it (case-sensitive) — e.g. "3dmodel.model" matches an entry
// stored as "3D/3dmodel.model". Returns the decompressed bytes, or a
// non-empty `error` (with empty `bytes`) if the archive can't be read, is
// malformed, or has no matching entry.
ZipExtractResult zipExtractBySuffix(const std::filesystem::path& archivePath,
                                    const std::string& entrySuffix);

} // namespace chisel::io
