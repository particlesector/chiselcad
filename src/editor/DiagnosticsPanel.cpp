#include "DiagnosticsPanel.h"
#include "ExternalEditor.h"
#include <imgui.h>

namespace chisel::editor {

void DiagnosticsPanel::setDiagnostics(std::vector<chisel::lang::Diagnostic> diags) {
    m_diags = std::move(diags);
}

void DiagnosticsPanel::setScadPath(std::filesystem::path path) {
    m_scadPath = std::move(path);
}

void DiagnosticsPanel::drawInline() {
    if (m_diags.empty()) {
        ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "No errors.");
        return;
    }

    for (int i = 0; i < static_cast<int>(m_diags.size()); ++i) {
        const auto& d = m_diags[i];
        ImVec4 col = (d.level == chisel::lang::DiagLevel::Error)
            ? ImVec4{1.0f, 0.3f, 0.3f, 1.0f}
            : ImVec4{1.0f, 0.8f, 0.3f, 1.0f};

        ImGui::PushStyleColor(ImGuiCol_Text, col);

        // Diagnostics with no file and no source location are runtime warnings
        // (e.g. geometry analysis) — show as plain text with no line:col prefix.
        const bool isRuntimeWarning = d.filePath.empty() &&
                                      d.loc == (chisel::lang::SourceLoc{});

        if (isRuntimeWarning) {
            ImGui::TextUnformatted(d.message.c_str());
        } else {
            char label[512];
            std::snprintf(label, sizeof(label), "%d:%d: %s##diag%d",
                d.loc.line + 1, d.loc.col + 1, d.message.c_str(), i);

            if (ImGui::Selectable(label)) {
                std::filesystem::path filePath =
                    d.filePath.empty() ? m_scadPath : std::filesystem::path(d.filePath);
                if (!filePath.empty())
                    openInExternalEditor(filePath,
                        static_cast<int>(d.loc.line + 1),
                        static_cast<int>(d.loc.col + 1));
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to open in editor");
        }

        ImGui::PopStyleColor();
    }
}

void DiagnosticsPanel::draw() {
    ImGui::Begin("Diagnostics");
    drawInline();
    ImGui::End();
}

} // namespace chisel::editor
