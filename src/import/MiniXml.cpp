#include "MiniXml.h"

#include <cctype>

namespace chisel::io {

namespace {

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

std::string trim(const std::string& s) {
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

std::vector<XmlEvent> tokenizeXml(const std::string& src) {
    std::vector<XmlEvent> events;
    std::size_t i = 0;
    while (i < src.size()) {
        std::size_t lt = src.find('<', i);
        if (lt == std::string::npos) {
            std::string text = trim(src.substr(i));
            if (!text.empty())
                events.push_back({XmlEvent::Kind::Text, "", {}, decodeEntities(text)});
            break;
        }
        if (lt > i) {
            std::string text = trim(src.substr(i, lt - i));
            if (!text.empty())
                events.push_back({XmlEvent::Kind::Text, "", {}, decodeEntities(text)});
        }
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
        std::size_t end = src.find('>', lt);
        if (end == std::string::npos)
            break;

        if (lt + 1 < src.size() && src[lt + 1] == '/') {
            std::string tag = trim(src.substr(lt + 2, end - lt - 2));
            events.push_back({XmlEvent::Kind::End, tag, {}, ""});
            i = end + 1;
            continue;
        }

        std::string body = src.substr(lt + 1, end - lt - 1);
        bool selfClose = !body.empty() && body.back() == '/';
        if (selfClose)
            body.pop_back();

        std::size_t p = 0;
        while (p < body.size() && !std::isspace(static_cast<unsigned char>(body[p])))
            ++p;
        XmlEvent ev;
        ev.kind = XmlEvent::Kind::Start;
        ev.tag = body.substr(0, p);

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
            ev.attrs[name] = decodeEntities(value);
        }
        events.push_back(ev);
        if (selfClose)
            events.push_back({XmlEvent::Kind::End, ev.tag, {}, ""});
        i = end + 1;
    }
    return events;
}

} // namespace chisel::io
