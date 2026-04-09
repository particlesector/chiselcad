#pragma once
#include "Token.h"
#include <string>
#include <vector>

namespace chisel::lang {

enum class DiagLevel : uint8_t { Info, Warning, Error };

struct Diagnostic {
    DiagLevel   level;
    std::string message;
    SourceLoc   loc;
    std::string filePath;
};

using DiagList = std::vector<Diagnostic>;

} // namespace chisel::lang
