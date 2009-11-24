//===-- ast/Stmt.cpp ------------------------------------------ -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "comma/ast/Expr.h"
#include "comma/ast/KeywordSelector.h"
#include "comma/ast/RangeAttrib.h"
#include "comma/ast/Stmt.h"

#include <iostream>

using namespace comma;
using llvm::dyn_cast;
using llvm::cast;
using llvm::isa;

//===----------------------------------------------------------------------===//
// ProcedureCallStmt
ProcedureCallStmt::ProcedureCallStmt(SubroutineRef *ref,
                                     Expr **posArgs, unsigned numPos,
                                     KeywordSelector **keys, unsigned numKeys)
    : Stmt(AST_ProcedureCallStmt),
      SubroutineCall(ref, posArgs, numPos, keys, numKeys),
      location(ref->getLocation())
{
    assert(ref->isResolved() && "Cannot form unresolved procedure calls!");
}

//===----------------------------------------------------------------------===//
// ReturnStmt
ReturnStmt::~ReturnStmt()
{
    if (returnExpr) delete returnExpr;
}

//===----------------------------------------------------------------------===//
// ForStmt

// The following methods are defined out of line to avoid inclusion of
// RangeAttrib.h.

ForStmt::ForStmt(Location loc, LoopDecl *iterationDecl, RangeAttrib *range)
    : Stmt(AST_ForStmt),
      location(loc),
      iterationDecl(iterationDecl),
      control(range, Range_Attribute_Control) { }

const Ast *ForStmt::getControl() const
{
    return control.getPointer();
}

Ast *ForStmt::getControl()
{
    return control.getPointer();
}

const RangeAttrib *ForStmt::getAttribControl() const
{
    if (control.getInt() == Range_Attribute_Control)
        return llvm::cast<RangeAttrib>(control.getPointer());
    return 0;
}

RangeAttrib *ForStmt::getAttribControl()
{
    if (control.getInt() == Range_Attribute_Control)
        return llvm::cast<RangeAttrib>(control.getPointer());
    return 0;
}

