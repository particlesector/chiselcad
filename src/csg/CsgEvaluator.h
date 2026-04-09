#pragma once
#include "CsgNode.h"
#include "lang/AST.h"
#include <glm/glm.hpp>

namespace chisel::csg {

// ---------------------------------------------------------------------------
// CsgEvaluator: walks a ParseResult and produces a CsgScene.
//
// Transform nodes are transparent — their matrices are accumulated and
// pushed into the CsgLeaf nodes so the CSG IR has no transform nodes.
// Boolean nodes are preserved; their children carry the combined matrix.
// ---------------------------------------------------------------------------
class CsgEvaluator {
public:
    CsgScene evaluate(const chisel::lang::ParseResult& result);

private:
    CsgNodePtr evalNode(const chisel::lang::AstNode& node, const glm::mat4& xform);
    CsgNodePtr evalPrimitive(const chisel::lang::PrimitiveNode& p, const glm::mat4& xform);
    CsgNodePtr evalBoolean(const chisel::lang::BooleanNode& b, const glm::mat4& xform);
    CsgNodePtr evalTransform(const chisel::lang::TransformNode& t, const glm::mat4& xform);

    static glm::mat4 makeMatrix(const chisel::lang::TransformNode& t);
};

} // namespace chisel::csg
