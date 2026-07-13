#include "AmfLoader.h"

#include "MiniXml.h"

#include <fstream>

namespace chisel::io {

RawAmfMesh loadAmfMesh(const std::filesystem::path& path) {
    RawAmfMesh out;

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        out.error = "Cannot open file: " + path.string();
        return out;
    }
    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::size_t objectVertexBase = 0;
    std::string curField; // "x"/"y"/"z"/"v1"/"v2"/"v3" while inside that leaf element
    double vx = 0.0, vy = 0.0, vz = 0.0;
    long long t1 = -1, t2 = -1, t3 = -1;

    for (const XmlEvent& ev : tokenizeXml(src)) {
        switch (ev.kind) {
        case XmlEvent::Kind::Start:
            if (ev.tag == "object") {
                objectVertexBase = out.positions.size();
            } else if (ev.tag == "coordinates") {
                vx = vy = vz = 0.0;
            } else if (ev.tag == "triangle") {
                t1 = t2 = t3 = -1;
            } else if (ev.tag == "x" || ev.tag == "y" || ev.tag == "z" || ev.tag == "v1" ||
                       ev.tag == "v2" || ev.tag == "v3") {
                curField = ev.tag;
            }
            break;

        case XmlEvent::Kind::Text:
            if (curField.empty())
                break;
            try {
                if (curField == "x")
                    vx = std::stod(ev.text);
                else if (curField == "y")
                    vy = std::stod(ev.text);
                else if (curField == "z")
                    vz = std::stod(ev.text);
                else if (curField == "v1")
                    t1 = std::stoll(ev.text);
                else if (curField == "v2")
                    t2 = std::stoll(ev.text);
                else if (curField == "v3")
                    t3 = std::stoll(ev.text);
            } catch (...) {
            }
            break;

        case XmlEvent::Kind::End:
            if (ev.tag == "x" || ev.tag == "y" || ev.tag == "z" || ev.tag == "v1" ||
                ev.tag == "v2" || ev.tag == "v3") {
                curField.clear();
            } else if (ev.tag == "vertex") {
                out.positions.emplace_back(static_cast<float>(vx), static_cast<float>(vy),
                                           static_cast<float>(vz));
            } else if (ev.tag == "triangle") {
                if (t1 >= 0 && t2 >= 0 && t3 >= 0) {
                    out.indices.push_back(
                        static_cast<uint32_t>(objectVertexBase + static_cast<std::size_t>(t1)));
                    out.indices.push_back(
                        static_cast<uint32_t>(objectVertexBase + static_cast<std::size_t>(t2)));
                    out.indices.push_back(
                        static_cast<uint32_t>(objectVertexBase + static_cast<std::size_t>(t3)));
                }
            }
            break;
        }
    }

    if (out.positions.empty())
        out.error = "AMF file contains no <vertex> geometry";
    else if (out.indices.empty())
        out.error = "AMF file contains no <triangle> geometry";

    return out;
}

} // namespace chisel::io
