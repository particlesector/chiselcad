#include "ZipReader.h"

#include <cstdint>
#include <fstream>
#include <zlib.h>

namespace chisel::io {

namespace {

constexpr uint32_t kEocdSig = 0x06054b50;
constexpr uint32_t kCentralSig = 0x02014b50;
constexpr uint32_t kLocalSig = 0x04034b50;
constexpr std::size_t kEocdMinSize = 22;
constexpr std::size_t kMaxCommentSize = 65535;

uint16_t readU16(const std::string& s, std::size_t off) {
    return static_cast<uint16_t>(static_cast<unsigned char>(s[off]) |
                                 (static_cast<unsigned char>(s[off + 1]) << 8));
}
uint32_t readU32(const std::string& s, std::size_t off) {
    return static_cast<uint32_t>(static_cast<unsigned char>(s[off])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(s[off + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(s[off + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(s[off + 3])) << 24);
}

// Raw DEFLATE (no zlib/gzip wrapper) — the compression zip entries use.
bool inflateRaw(const std::string& compressed, std::size_t uncompressedSize, std::string& out) {
    out.assign(uncompressedSize, '\0');
    if (uncompressedSize == 0)
        return true;

    z_stream strm{};
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return false;

    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    strm.avail_in = static_cast<uInt>(compressed.size());
    strm.next_out = reinterpret_cast<Bytef*>(out.data());
    strm.avail_out = static_cast<uInt>(out.size());

    int ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    return ret == Z_STREAM_END;
}

} // namespace

ZipExtractResult zipExtractBySuffix(const std::filesystem::path& archivePath,
                                    const std::string& entrySuffix) {
    ZipExtractResult out;

    std::ifstream f(archivePath, std::ios::binary);
    if (!f) {
        out.error = "Cannot open file: " + archivePath.string();
        return out;
    }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() < kEocdMinSize) {
        out.error = "not a valid ZIP archive (too small)";
        return out;
    }

    // Find the End Of Central Directory record by scanning backward for its
    // signature — it's always the last thing in the file, but may be
    // preceded by an (up to 65535-byte) comment field.
    const std::size_t searchStart = data.size() >= (kEocdMinSize + kMaxCommentSize)
                                        ? data.size() - kEocdMinSize - kMaxCommentSize
                                        : 0;
    std::size_t eocdPos = std::string::npos;
    for (std::size_t p = data.size() - kEocdMinSize + 1; p-- > searchStart;) {
        if (readU32(data, p) == kEocdSig) {
            eocdPos = p;
            break;
        }
    }
    if (eocdPos == std::string::npos) {
        out.error = "not a valid ZIP archive (no end-of-central-directory record found)";
        return out;
    }

    uint16_t totalEntries = readU16(data, eocdPos + 10);
    uint32_t centralDirStart = readU32(data, eocdPos + 16);

    std::size_t pos = centralDirStart;
    bool found = false;
    uint16_t method = 0;
    uint32_t compSize = 0, uncompSize = 0, localOffset = 0;

    for (uint16_t i = 0; i < totalEntries; ++i) {
        if (pos + 46 > data.size() || readU32(data, pos) != kCentralSig)
            break;
        uint16_t m = readU16(data, pos + 10);
        uint32_t compSz = readU32(data, pos + 20);
        uint32_t uncompSz = readU32(data, pos + 24);
        uint16_t nameLen = readU16(data, pos + 28);
        uint16_t extraLen = readU16(data, pos + 30);
        uint16_t commentLen = readU16(data, pos + 32);
        uint32_t localOff = readU32(data, pos + 42);
        if (pos + 46 + nameLen > data.size())
            break;
        std::string name = data.substr(pos + 46, nameLen);

        if (!found && name.size() >= entrySuffix.size() &&
            name.compare(name.size() - entrySuffix.size(), entrySuffix.size(), entrySuffix) == 0) {
            found = true;
            method = m;
            compSize = compSz;
            uncompSize = uncompSz;
            localOffset = localOff;
        }

        pos += 46 + nameLen + extraLen + commentLen;
    }

    if (!found) {
        out.error = "ZIP archive has no entry matching '" + entrySuffix + "'";
        return out;
    }

    if (localOffset + 30 > data.size() || readU32(data, localOffset) != kLocalSig) {
        out.error = "malformed ZIP local file header";
        return out;
    }
    uint16_t localNameLen = readU16(data, localOffset + 26);
    uint16_t localExtraLen = readU16(data, localOffset + 28);
    std::size_t dataStart = localOffset + 30 + localNameLen + localExtraLen;
    if (dataStart + compSize > data.size()) {
        out.error = "malformed ZIP entry (compressed data extends past end of file)";
        return out;
    }
    std::string compressed = data.substr(dataStart, compSize);

    if (method == 0) { // stored
        out.bytes = std::move(compressed);
    } else if (method == 8) { // deflate
        if (!inflateRaw(compressed, uncompSize, out.bytes)) {
            out.error = "failed to decompress ZIP entry (corrupt DEFLATE stream)";
            out.bytes.clear();
        }
    } else {
        out.error = "unsupported ZIP compression method (" + std::to_string(method) +
                    ") — only stored/deflate are supported";
    }
    return out;
}

} // namespace chisel::io
