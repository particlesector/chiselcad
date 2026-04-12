#pragma once
#include "lang/Diagnostic.h"
#include <string>
#include <vector>

namespace chisel::editor {

// ---------------------------------------------------------------------------
// DiagnosticsPanel — ImGui panel that shows parse/eval errors.
// ---------------------------------------------------------------------------
class DiagnosticsPanel {
public:
    void setDiagnostics(std::vector<chisel::lang::Diagnostic> diags);
    void draw(); // call inside an ImGui frame

private:
    std::vector<chisel::lang::Diagnostic> m_diags;
};

} // namespace chisel::editor
