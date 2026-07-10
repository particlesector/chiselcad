#pragma once
#include "CsgNode.h"
#include "lang/AST.h"
#include "lang/Interpreter.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace chisel::csg {

// ---------------------------------------------------------------------------
// CsgEvaluator: walks a ParseResult and produces a CsgScene.
//
// The Interpreter resolves ExprPtr params to concrete doubles before the
// CSG IR is built. Transform nodes are folded into leaf matrices; color()
// nodes are folded the same way into each node's ColorAttr.
// ---------------------------------------------------------------------------
class CsgEvaluator {
public:
    // Base directory for resolving relative import()/surface() paths. Set by
    // the caller (MeshBuilder) to the root .scad file's directory before
    // calling evaluate(); defaults to the process's current directory.
    // Caveat: since include<>/use<> flatten a multi-file ParseResult into
    // one before CsgEvaluator ever sees it (SourceLoader.h), an import()
    // written inside a used/included file also resolves against the ROOT
    // file's directory, not its own — the AST carries no per-node file
    // identity to do otherwise (same root cause as assert()/echo()
    // diagnostics not carrying a filePath from such files).
    std::filesystem::path baseDir;

    // Convenience overload: creates a default Interpreter internally.
    CsgScene evaluate(const chisel::lang::ParseResult& result);

    // Full overload: uses a pre-populated Interpreter (has assignments loaded).
    CsgScene evaluate(const chisel::lang::ParseResult& result,
                      chisel::lang::Interpreter& interp);

private:
    chisel::lang::Interpreter* m_interp = nullptr; // non-owning, set during evaluate()

    // Module definitions indexed by name — populated at evaluate() entry.
    std::unordered_map<std::string, const chisel::lang::ModuleDef*> m_moduleDefs;

    // Stack of children vectors for children() access inside module bodies.
    // Each user module call pushes its children; evalChildren pops/re-pushes for nesting.
    std::vector<const std::vector<chisel::lang::AstNodePtr>*> m_childrenStack;

    // Non-owning pointer to the scene being built — valid during evaluate().
    CsgScene* m_scene = nullptr;

    CsgNodePtr evalNode(const chisel::lang::AstNode& node, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalPrimitive(const chisel::lang::PrimitiveNode& p, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalBoolean(const chisel::lang::BooleanNode& b, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalTransform(const chisel::lang::TransformNode& t, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalIf(const chisel::lang::IfNode& n, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalFor(const chisel::lang::ForNode& n, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalModuleCall(const chisel::lang::ModuleCallNode& n, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalChildren(const chisel::lang::ModuleCallNode& n, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalExtrusion(const chisel::lang::ExtrusionNode& e, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalLet(const chisel::lang::LetNode& n, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalColor(const chisel::lang::ColorNode& n, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalOffset(const chisel::lang::OffsetNode& n, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalProjection(const chisel::lang::ProjectionNode& n, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalImport(const chisel::lang::ModuleCallNode& call, const glm::mat4& xform, const ColorAttr& color);
    CsgNodePtr evalSurface(const chisel::lang::ModuleCallNode& call, const glm::mat4& xform, const ColorAttr& color);

    // Shared by evalImport()/evalSurface(): pushes an error Diagnostic (if a
    // scene is being built) at the given source location.
    void reportEvalError(const chisel::lang::SourceLoc& loc, std::string msg);

    // Shared by evalImport()/evalSurface(): resolves and validates the
    // file-path argument common to both builtins. See definition for the
    // positional-vs-named precedence rule.
    std::optional<std::filesystem::path> resolveFilePathArg(
        const chisel::lang::ModuleCallNode& call, std::string_view builtinName);

    glm::mat4 makeMatrix(const chisel::lang::TransformNode& t) const;
    bool resolveColor(const chisel::lang::Value& c, glm::vec4& out) const;

    static std::string formatValue(const chisel::lang::Value& v);
};

} // namespace chisel::csg
