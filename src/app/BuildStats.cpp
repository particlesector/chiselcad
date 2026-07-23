#include "BuildStats.h"

#include <nlohmann/json.hpp>

namespace chisel::app {

static const char* levelName(lang::DiagLevel level) {
    switch (level) {
        case lang::DiagLevel::Info:    return "info";
        case lang::DiagLevel::Warning: return "warning";
        case lang::DiagLevel::Error:   return "error";
    }
    return "info";
}

std::string buildResultToJson(const BuildResult& result, int indent) {
    nlohmann::json j;
    j["ok"] = result.ok();
    j["errorMsg"] = result.errorMsg;
    j["volume"] = result.volume;
    j["surfaceArea"] = result.surfaceArea;
    j["triCount"] = result.triCount;
    j["vertCount"] = result.vertCount;
    j["elapsedMs"] = result.elapsedMs;

    auto diags = nlohmann::json::array();
    for (const auto& d : result.diags) {
        nlohmann::json dj;
        dj["level"] = levelName(d.level);
        dj["message"] = d.message;
        dj["file"] = d.filePath;
        dj["line"] = d.loc.line;
        dj["col"] = d.loc.col;
        diags.push_back(std::move(dj));
    }
    j["diagnostics"] = std::move(diags);

    return j.dump(indent);
}

} // namespace chisel::app
