#pragma once
#include "CsgNode.h"
#include "lang/AST.h"
#include "lang/Interpreter.h"
#include <glm/glm.hpp>

namespace chisel::csg {

// ---------------------------------------------------------------------------
// CsgEvaluator: walks a ParseResult and produces a CsgScene.
//
// The Interpreter resolves ExprPtr params to concrete doubles before the
// CSG IR is built. Transform nodes are folded into leaf matrices.
// ---------------------------------------------------------------------------
class CsgEvaluator {
public:
    // Convenience overload: creates a default Interpreter internally.
    CsgScene evaluate(const chisel::lang::ParseResult& result);

    // Full overload: uses a pre-populated Interpreter (has assignments loaded).
    CsgScene evaluate(const chisel::lang::ParseResult& result,
                      chisel::lang::Interpreter& interp);

private:
    chisel::lang::Interpreter* m_interp = nullptr; // non-owning, set during evaluate()

    CsgNodePtr evalNode(const chisel::lang::AstNode& node, const glm::mat4& xform);
    CsgNodePtr evalPrimitive(const chisel::lang::PrimitiveNode& p, const glm::mat4& xform);
    CsgNodePtr evalBoolean(const chisel::lang::BooleanNode& b, const glm::mat4& xform);
    CsgNodePtr evalTransform(const chisel::lang::TransformNode& t, const glm::mat4& xform);
    CsgNodePtr evalIf(const chisel::lang::IfNode& n, const glm::mat4& xform);

    glm::mat4 makeMatrix(const chisel::lang::TransformNode& t) const;
};

} // namespace chisel::csg
