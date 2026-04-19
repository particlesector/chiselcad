#pragma once
#include "lang/Diagnostic.h"
#include <filesystem>
#include <string>
#include <vector>

namespace chisel::editor {

// ---------------------------------------------------------------------------
// DiagnosticsPanel — ImGui panel that shows parse/eval errors.
// Clicking a row opens the file in VS Code at the error location.
// ---------------------------------------------------------------------------
class DiagnosticsPanel {
public:
    void setDiagnostics(std::vector<chisel::lang::Diagnostic> diags);
    void setScadPath(std::filesystem::path path);
    void draw();       // standalone window
    void drawInline(); // content only — call inside an existing Begin/End

private:
    std::vector<chisel::lang::Diagnostic> m_diags;
    std::filesystem::path                 m_scadPath;
};

} // namespace chisel::editor
