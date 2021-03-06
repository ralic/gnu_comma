//===-- ast/ExprVisitor.h ------------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009-2010, Stephen Wilson
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
/// \file
///
/// \brief This file provides a virtual class used for implementing the vistor
/// pattern across expression nodes.
///
//===----------------------------------------------------------------------===//

#ifndef COMMA_AST_EXPRVISITOR_HDR_GUARD
#define COMMA_AST_EXPRVISITOR_HDR_GUARD

#include "comma/ast/AstBase.h"

namespace comma {

class ExprVisitor {

public:
    virtual ~ExprVisitor() { }

    virtual void visitAst(Ast *node);
    virtual void visitExpr(Expr *node);
    virtual void visitAggregateExpr(AggregateExpr *node);
    virtual void visitAttribExpr(AttribExpr *node);
    virtual void visitDeclRefExpr(DeclRefExpr *node);
    virtual void visitFunctionCallExpr(FunctionCallExpr *node);
    virtual void visitIndexedArrayExpr(IndexedArrayExpr *node);
    virtual void visitSelectedExpr(SelectedExpr *node);
    virtual void visitIntegerLiteral(IntegerLiteral *node);
    virtual void visitStringLiteral(StringLiteral *node);
    virtual void visitConversionExpr(ConversionExpr *node);
    virtual void visitNullExpr(NullExpr *node);
    virtual void visitQualifiedExpr(QualifiedExpr *node);
    virtual void visitDereferenceExpr(DereferenceExpr *node);
    virtual void visitAllocatorExpr(AllocatorExpr *node);
    virtual void visitDiamondExpr(DiamondExpr *node);

    /// Visitors over AttribExpr nodes.
    virtual void visitFirstAE(FirstAE *node);
    virtual void visitFirstArrayAE(FirstArrayAE *node);
    virtual void visitLastArrayAE(LastArrayAE *node);
    virtual void visitLengthAE(LengthAE *node);
    virtual void visitLastAE(LastAE *node);
};

} // end comma namespace.

#endif
