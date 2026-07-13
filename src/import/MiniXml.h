#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace chisel::io {

// ---------------------------------------------------------------------------
// MiniXml — a minimal flat SAX-like XML tokenizer shared by ThreeMfLoader
// and AmfLoader (3MF's geometry lives in element attributes, AMF's lives in
// element text content — this handles both). NOT a validating parser: no
// DTD, no namespace resolution beyond keeping "prefix:local" as one literal
// tag name, no distinction between CDATA and ordinary text. Comments and
// `<?...?>`/`<!...>` declarations are skipped. Good enough for AMF/3MF's
// simple, shallow geometry XML — not a general-purpose XML library.
// ---------------------------------------------------------------------------
struct XmlEvent {
    enum class Kind { Start, End, Text } kind;
    std::string tag;                                    // Start/End
    std::unordered_map<std::string, std::string> attrs; // Start
    std::string text;                                   // Text (trimmed, entity-decoded)
};

// Tokenizes `src` into a flat stream of Start/End/Text events in document
// order. A self-closing tag (`<tag/>`) emits a Start immediately followed
// by an End, so callers never need to special-case self-closing vs.
// open+close forms.
std::vector<XmlEvent> tokenizeXml(const std::string& src);

} // namespace chisel::io
