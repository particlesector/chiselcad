#include "DiagnosticsPanel.h"
#include <imgui.h>

namespace chisel::editor {

void DiagnosticsPanel::setDiagnostics(std::vector<chisel::lang::Diagnostic> diags) {
    m_diags = std::move(diags);
}

void DiagnosticsPanel::draw() {
    ImGui::Begin("Diagnostics");
    if (m_diags.empty()) {
        ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "No errors.");
    } else {
        for (const auto& d : m_diags) {
            ImVec4 col = (d.level == chisel::lang::DiagLevel::Error)
                ? ImVec4{1.0f, 0.3f, 0.3f, 1.0f}
                : ImVec4{1.0f, 0.8f, 0.3f, 1.0f};
            ImGui::TextColored(col, "%d:%d: %s",
                d.loc.line, d.loc.col, d.message.c_str());
        }
    }
    ImGui::End();
}

} // namespace chisel::editor
