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

    // Stack frame for children() access inside module bodies: the children
    // AST nodes from the call site, plus a snapshot of the *caller's*
    // variable environment (taken before the callee's own params were
    // bound) so children() evaluates that AST using caller-visible
    // variables rather than the callee's parameter bindings.
    //
    // callerEnv points at evalModuleCall's local `savedEnv`, which is
    // declared before this frame is pushed and not touched again until
    // after the matching pop — its lifetime strictly encloses the frame's
    // time on the stack, so a pointer avoids an env-map copy on every
    // module call (only children(), when actually invoked, pays for a copy).
    struct ChildrenFrame {
        const std::vector<chisel::lang::AstNodePtr>* children;
        const std::unordered_map<std::string, chisel::lang::Value>* callerEnv;
    };
    // Each user module call pushes its children; evalChildren pops/re-pushes for nesting.
    std::vector<ChildrenFrame> m_childrenStack;

    // Non-owning pointer to the scene being built — valid during evaluate().
    CsgScene* m_scene = nullptr;

    // Guards against unbounded recursion (missing base case, or mutual
    // recursion between modules) blowing the native call stack. Module-call
    // stack frames run larger than the Interpreter's function-call frames
    // (glm::mat4 passed by value, ColorAttr, CsgNodePtr vectors, ...), so
    // this cap is lower than kMaxCallDepth in Interpreter.h even though both
    // target the same worst-case 1 MiB MSVC default thread stack — see that
    // comment for the measurement methodology. Empirically this path
    // overflowed a 1 MiB stack between depth 500-600 in an unguarded GCC
    // Release build.
    static constexpr int kMaxModuleDepth = 100;
    int m_moduleDepth = 0;

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
    CsgNodePtr evalText(const chisel::lang::ModuleCallNode& call, const glm::mat4& xform, const ColorAttr& color);

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
