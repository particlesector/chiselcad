#include "SvgLoader.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace chisel::io {

namespace {

constexpr int kCircleSegments = 64;
constexpr int kCurveSegments = 12; // per Bezier segment
constexpr double kPi = 3.14159265358979323846;

std::string decodeEntities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;") == 0) {
                out += '&';
                i += 5;
                continue;
            }
            if (s.compare(i, 4, "&lt;") == 0) {
                out += '<';
                i += 4;
                continue;
            }
            if (s.compare(i, 4, "&gt;") == 0) {
                out += '>';
                i += 4;
                continue;
            }
            if (s.compare(i, 6, "&quot;") == 0) {
                out += '"';
                i += 6;
                continue;
            }
            if (s.compare(i, 6, "&apos;") == 0) {
                out += '\'';
                i += 6;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

// ---------------------------------------------------------------------------
// XmlTag — a minimal scanner tailored to SVG's shape elements (rect/circle/
// ellipse/polygon/path), all of which are leaf elements with no meaningful
// child content for our purposes. NOT a general XML parser: no namespaces,
// no CDATA, no nested element tree — it just walks the byte stream for
// `<tagname ...>` tokens and their attribute maps, skipping comments,
// `<?xml?>`/`<!DOCTYPE>` headers, and closing tags as opaque bytes.
// ---------------------------------------------------------------------------
struct XmlTag {
    std::string tag;
    std::unordered_map<std::string, std::string> attrs;

    std::string attr(const std::string& name, const std::string& def = "0") const {
        auto it = attrs.find(name);
        return it == attrs.end() ? def : it->second;
    }
    double attrNum(const std::string& name, double def = 0.0) const {
        auto it = attrs.find(name);
        if (it == attrs.end())
            return def;
        try {
            return std::stod(it->second);
        } catch (...) {
            return def;
        }
    }
};

std::vector<XmlTag> scanTags(const std::string& src) {
    std::vector<XmlTag> tags;
    std::size_t i = 0;
    while (i < src.size()) {
        std::size_t lt = src.find('<', i);
        if (lt == std::string::npos)
            break;
        if (src.compare(lt, 4, "<!--") == 0) {
            std::size_t end = src.find("-->", lt);
            i = (end == std::string::npos) ? src.size() : end + 3;
            continue;
        }
        if (lt + 1 < src.size() && (src[lt + 1] == '?' || src[lt + 1] == '!')) {
            std::size_t end = src.find('>', lt);
            i = (end == std::string::npos) ? src.size() : end + 1;
            continue;
        }
        if (lt + 1 < src.size() && src[lt + 1] == '/') {
            std::size_t end = src.find('>', lt);
            i = (end == std::string::npos) ? src.size() : end + 1;
            continue;
        }
        std::size_t end = src.find('>', lt);
        if (end == std::string::npos)
            break;
        std::string body = src.substr(lt + 1, end - lt - 1);
        if (!body.empty() && body.back() == '/')
            body.pop_back();
        i = end + 1;

        std::size_t p = 0;
        while (p < body.size() && !std::isspace(static_cast<unsigned char>(body[p])))
            ++p;
        XmlTag xt;
        xt.tag = body.substr(0, p);

        while (p < body.size()) {
            while (p < body.size() && std::isspace(static_cast<unsigned char>(body[p])))
                ++p;
            std::size_t nameStart = p;
            while (p < body.size() && body[p] != '=' &&
                   !std::isspace(static_cast<unsigned char>(body[p])))
                ++p;
            if (p == nameStart)
                break;
            std::string name = body.substr(nameStart, p - nameStart);
            while (p < body.size() && std::isspace(static_cast<unsigned char>(body[p])))
                ++p;
            if (p >= body.size() || body[p] != '=')
                continue; // valueless attribute — skip
            ++p;
            while (p < body.size() && std::isspace(static_cast<unsigned char>(body[p])))
                ++p;
            if (p >= body.size())
                break;
            char quote = body[p];
            if (quote != '"' && quote != '\'')
                break;
            ++p;
            std::size_t valStart = p;
            while (p < body.size() && body[p] != quote)
                ++p;
            std::string value = body.substr(valStart, p - valStart);
            if (p < body.size())
                ++p;
            xt.attrs[name] = decodeEntities(value);
        }
        tags.push_back(std::move(xt));
    }
    return tags;
}

// ---------------------------------------------------------------------------
// Path `d` attribute tokenizer/parser (M/m L/l H/h V/v C/c Q/q Z/z fully
// supported; S/s T/t/A/a parameter counts consumed but drawn as a straight
// line to their endpoint — see SvgLoader.h's scope note).
// ---------------------------------------------------------------------------
class PathScanner {
  public:
    explicit PathScanner(const std::string& d) : m_d(d) {}

    bool atEnd() {
        skipSep();
        return m_pos >= m_d.size();
    }

    // True if the next token is a command letter (vs. a number, meaning an
    // implicit repeat of the previous command).
    bool peekIsCommand() {
        skipSep();
        return m_pos < m_d.size() && std::isalpha(static_cast<unsigned char>(m_d[m_pos]));
    }
    char nextCommand() {
        skipSep();
        return m_d[m_pos++];
    }
    double nextNumber() {
        skipSep();
        std::size_t start = m_pos;
        if (m_pos < m_d.size() && (m_d[m_pos] == '+' || m_d[m_pos] == '-'))
            ++m_pos;
        while (m_pos < m_d.size() &&
               (std::isdigit(static_cast<unsigned char>(m_d[m_pos])) || m_d[m_pos] == '.'))
            ++m_pos;
        if (m_pos < m_d.size() && (m_d[m_pos] == 'e' || m_d[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_d.size() && (m_d[m_pos] == '+' || m_d[m_pos] == '-'))
                ++m_pos;
            while (m_pos < m_d.size() && std::isdigit(static_cast<unsigned char>(m_d[m_pos])))
                ++m_pos;
        }
        if (m_pos == start) {
            ++m_pos;
            return 0.0;
        } // malformed — advance to avoid an infinite loop
        try {
            return std::stod(m_d.substr(start, m_pos - start));
        } catch (...) {
            return 0.0;
        }
    }

  private:
    void skipSep() {
        while (m_pos < m_d.size() &&
               (std::isspace(static_cast<unsigned char>(m_d[m_pos])) || m_d[m_pos] == ','))
            ++m_pos;
    }
    const std::string& m_d;
    std::size_t m_pos = 0;
};

void flattenCubic(std::vector<glm::vec2>& out, glm::vec2 p0, glm::vec2 p1, glm::vec2 p2,
                  glm::vec2 p3) {
    for (int i = 1; i <= kCurveSegments; ++i) {
        double t = static_cast<double>(i) / kCurveSegments;
        double u = 1.0 - t;
        glm::vec2 pt = static_cast<float>(u * u * u) * p0 + static_cast<float>(3 * u * u * t) * p1 +
                       static_cast<float>(3 * u * t * t) * p2 + static_cast<float>(t * t * t) * p3;
        out.push_back(pt);
    }
}
void flattenQuad(std::vector<glm::vec2>& out, glm::vec2 p0, glm::vec2 p1, glm::vec2 p2) {
    for (int i = 1; i <= kCurveSegments; ++i) {
        double t = static_cast<double>(i) / kCurveSegments;
        double u = 1.0 - t;
        glm::vec2 pt = static_cast<float>(u * u) * p0 + static_cast<float>(2 * u * t) * p1 +
                       static_cast<float>(t * t) * p2;
        out.push_back(pt);
    }
}

// Parses a `d` attribute into closed subpaths only (those ending in Z/z).
std::vector<std::vector<glm::vec2>> parsePathData(const std::string& d) {
    std::vector<std::vector<glm::vec2>> closed;
    PathScanner sc(d);

    glm::vec2 cur{0.0f, 0.0f};
    glm::vec2 subpathStart{0.0f, 0.0f};
    std::vector<glm::vec2> current;
    bool haveSubpath = false;
    char cmd = 0;

    auto closeSubpath = [&] {
        if (haveSubpath && current.size() >= 3)
            closed.push_back(current);
        current.clear();
        haveSubpath = false;
    };

    while (!sc.atEnd()) {
        if (sc.peekIsCommand())
            cmd = sc.nextCommand();
        bool relative = std::islower(static_cast<unsigned char>(cmd)) != 0;
        char C = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));

        switch (C) {
        case 'M': {
            double x = sc.nextNumber(), y = sc.nextNumber();
            glm::vec2 p{static_cast<float>(x), static_cast<float>(y)};
            cur = relative ? cur + p : p;
            closeSubpath();
            current.push_back(cur);
            subpathStart = cur;
            haveSubpath = true;
            cmd = relative ? 'l' : 'L'; // subsequent bare coordinate pairs are implicit lineto
            break;
        }
        case 'L': {
            double x = sc.nextNumber(), y = sc.nextNumber();
            glm::vec2 p{static_cast<float>(x), static_cast<float>(y)};
            cur = relative ? cur + p : p;
            current.push_back(cur);
            break;
        }
        case 'H': {
            double x = sc.nextNumber();
            cur = relative ? glm::vec2{cur.x + static_cast<float>(x), cur.y}
                           : glm::vec2{static_cast<float>(x), cur.y};
            current.push_back(cur);
            break;
        }
        case 'V': {
            double y = sc.nextNumber();
            cur = relative ? glm::vec2{cur.x, cur.y + static_cast<float>(y)}
                           : glm::vec2{cur.x, static_cast<float>(y)};
            current.push_back(cur);
            break;
        }
        case 'C': {
            glm::vec2 p1{static_cast<float>(sc.nextNumber()), static_cast<float>(sc.nextNumber())};
            glm::vec2 p2{static_cast<float>(sc.nextNumber()), static_cast<float>(sc.nextNumber())};
            glm::vec2 p3{static_cast<float>(sc.nextNumber()), static_cast<float>(sc.nextNumber())};
            if (relative) {
                p1 += cur;
                p2 += cur;
                p3 += cur;
            }
            flattenCubic(current, cur, p1, p2, p3);
            cur = p3;
            break;
        }
        case 'Q': {
            glm::vec2 p1{static_cast<float>(sc.nextNumber()), static_cast<float>(sc.nextNumber())};
            glm::vec2 p2{static_cast<float>(sc.nextNumber()), static_cast<float>(sc.nextNumber())};
            if (relative) {
                p1 += cur;
                p2 += cur;
            }
            flattenQuad(current, cur, p1, p2);
            cur = p2;
            break;
        }
        case 'S': { // smooth cubic — reflection not modeled; consumed and drawn straight-ish via
                    // the two control points as given
            glm::vec2 p2{static_cast<float>(sc.nextNumber()), static_cast<float>(sc.nextNumber())};
            glm::vec2 p3{static_cast<float>(sc.nextNumber()), static_cast<float>(sc.nextNumber())};
            if (relative) {
                p2 += cur;
                p3 += cur;
            }
            flattenCubic(current, cur, cur, p2, p3);
            cur = p3;
            break;
        }
        case 'T': { // smooth quadratic — reflection not modeled
            glm::vec2 p2{static_cast<float>(sc.nextNumber()), static_cast<float>(sc.nextNumber())};
            if (relative)
                p2 += cur;
            flattenQuad(current, cur, cur, p2);
            cur = p2;
            break;
        }
        case 'A': { // elliptical arc — parameters consumed for correct
                    // stream position, drawn as a straight line to the
                    // endpoint rather than the true arc (see SvgLoader.h).
            sc.nextNumber();
            sc.nextNumber();
            sc.nextNumber();
            sc.nextNumber();
            sc.nextNumber();
            double x = sc.nextNumber(), y = sc.nextNumber();
            glm::vec2 p{static_cast<float>(x), static_cast<float>(y)};
            cur = relative ? cur + p : p;
            current.push_back(cur);
            break;
        }
        case 'Z': {
            if (haveSubpath)
                current.push_back(subpathStart);
            closeSubpath();
            cur = subpathStart;
            break;
        }
        default:
            // Unrecognized command — bail out of this path rather than
            // looping forever on unconsumed input.
            closeSubpath();
            return closed;
        }
    }
    closeSubpath();
    return closed;
}

} // namespace

RawPolygon2D loadSvgPaths(const std::filesystem::path& path) {
    RawPolygon2D out;

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        out.error = "Cannot open file: " + path.string();
        return out;
    }
    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::vector<std::vector<glm::vec2>> closedPaths;

    for (const XmlTag& t : scanTags(src)) {
        if (t.tag == "rect") {
            double x = t.attrNum("x"), y = t.attrNum("y");
            double w = t.attrNum("width"), h = t.attrNum("height");
            if (w <= 0.0 || h <= 0.0)
                continue;
            closedPaths.push_back({
                {static_cast<float>(x), static_cast<float>(y)},
                {static_cast<float>(x + w), static_cast<float>(y)},
                {static_cast<float>(x + w), static_cast<float>(y + h)},
                {static_cast<float>(x), static_cast<float>(y + h)},
            });
        } else if (t.tag == "circle") {
            double cx = t.attrNum("cx"), cy = t.attrNum("cy"), r = t.attrNum("r");
            if (r <= 0.0)
                continue;
            std::vector<glm::vec2> pts;
            pts.reserve(kCircleSegments);
            for (int s = 0; s < kCircleSegments; ++s) {
                double a = 2.0 * kPi * static_cast<double>(s) / kCircleSegments;
                pts.emplace_back(static_cast<float>(cx + r * std::cos(a)),
                                 static_cast<float>(cy + r * std::sin(a)));
            }
            closedPaths.push_back(std::move(pts));
        } else if (t.tag == "ellipse") {
            double cx = t.attrNum("cx"), cy = t.attrNum("cy"), rx = t.attrNum("rx"),
                   ry = t.attrNum("ry");
            if (rx <= 0.0 || ry <= 0.0)
                continue;
            std::vector<glm::vec2> pts;
            pts.reserve(kCircleSegments);
            for (int s = 0; s < kCircleSegments; ++s) {
                double a = 2.0 * kPi * static_cast<double>(s) / kCircleSegments;
                pts.emplace_back(static_cast<float>(cx + rx * std::cos(a)),
                                 static_cast<float>(cy + ry * std::sin(a)));
            }
            closedPaths.push_back(std::move(pts));
        } else if (t.tag == "polygon") {
            // SVG's "points" attribute allows comma and/or whitespace
            // interchangeably between values — normalize commas to spaces,
            // then read a flat number list and pair it up (x,y),(x,y),...
            std::string ptsAttr = t.attr("points", "");
            for (char& c : ptsAttr)
                if (c == ',')
                    c = ' ';
            std::istringstream ss(ptsAttr);
            std::vector<double> nums;
            double v;
            while (ss >> v)
                nums.push_back(v);
            std::vector<glm::vec2> pts;
            for (std::size_t k = 0; k + 1 < nums.size(); k += 2)
                pts.emplace_back(static_cast<float>(nums[k]), static_cast<float>(nums[k + 1]));
            if (pts.size() >= 3)
                closedPaths.push_back(std::move(pts));
        } else if (t.tag == "path") {
            auto subpaths = parsePathData(t.attr("d", ""));
            for (auto& sp : subpaths)
                closedPaths.push_back(std::move(sp));
        }
        // line / polyline: always open — skipped, see SvgLoader.h.
    }

    if (closedPaths.empty()) {
        out.error = "SVG file contains no closed shapes (rect/circle/ellipse/polygon, or a path "
                    "with a 'Z')";
        return out;
    }

    for (auto& path : closedPaths) {
        std::vector<int> indices;
        indices.reserve(path.size());
        for (auto& pt : path) {
            indices.push_back(static_cast<int>(out.points.size()));
            out.points.push_back(pt);
        }
        out.paths.push_back(std::move(indices));
    }
    return out;
}

} // namespace chisel::io
