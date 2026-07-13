#include "ThreeMfLoader.h"

#include "MiniXml.h"
#include "ZipReader.h"

namespace chisel::io {

namespace {

double attrNum(const XmlEvent& ev, const char* name) {
    auto it = ev.attrs.find(name);
    if (it == ev.attrs.end())
        return 0.0;
    try {
        return std::stod(it->second);
    } catch (...) {
        return 0.0;
    }
}
long long attrInt(const XmlEvent& ev, const char* name) {
    auto it = ev.attrs.find(name);
    if (it == ev.attrs.end())
        return -1;
    try {
        return std::stoll(it->second);
    } catch (...) {
        return -1;
    }
}

} // namespace

RawThreeMfMesh loadThreeMfMesh(const std::filesystem::path& path) {
    RawThreeMfMesh out;

    ZipExtractResult zr = zipExtractBySuffix(path, "3dmodel.model");
    if (!zr.error.empty()) {
        out.error = zr.error;
        return out;
    }

    std::size_t objectVertexBase = 0;
    for (const XmlEvent& ev : tokenizeXml(zr.bytes)) {
        if (ev.kind != XmlEvent::Kind::Start)
            continue;

        if (ev.tag == "object") {
            objectVertexBase = out.positions.size();
        } else if (ev.tag == "vertex") {
            out.positions.emplace_back(static_cast<float>(attrNum(ev, "x")),
                                       static_cast<float>(attrNum(ev, "y")),
                                       static_cast<float>(attrNum(ev, "z")));
        } else if (ev.tag == "triangle") {
            long long v1 = attrInt(ev, "v1"), v2 = attrInt(ev, "v2"), v3 = attrInt(ev, "v3");
            if (v1 < 0 || v2 < 0 || v3 < 0)
                continue; // malformed triangle — skip rather than crash
            out.indices.push_back(
                static_cast<uint32_t>(objectVertexBase + static_cast<std::size_t>(v1)));
            out.indices.push_back(
                static_cast<uint32_t>(objectVertexBase + static_cast<std::size_t>(v2)));
            out.indices.push_back(
                static_cast<uint32_t>(objectVertexBase + static_cast<std::size_t>(v3)));
        }
    }

    if (out.positions.empty())
        out.error = "3MF file contains no <vertex> geometry";
    else if (out.indices.empty())
        out.error = "3MF file contains no <triangle> geometry";

    return out;
}

} // namespace chisel::io
