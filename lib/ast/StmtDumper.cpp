//===-- ast/StmtDumper.cpp ------------------------------------ -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "StmtDumper.h"
#include "comma/ast/Decl.h"
#include "comma/ast/Expr.h"
#include "comma/ast/Stmt.h"

#include "llvm/Support/Format.h"

using namespace comma;

using llvm::dyn_cast;
using llvm::cast;
using llvm::isa;


llvm::raw_ostream &StmtDumper::dump(Stmt *stmt, unsigned level)
{
    unsigned savedLevel = indentLevel;
    indentLevel = level;
    visitStmt(stmt);
    indentLevel = savedLevel;
    return S;
}

llvm::raw_ostream &StmtDumper::dumpAST(Ast *node)
{
    return dumper->dump(node, indentLevel);
}

void StmtDumper::visitStmtSequence(StmtSequence *node)
{
    printHeader(node);
    indent();
    for (StmtSequence::StmtIter I = node->beginStatements();
         I != node->endStatements(); ++I) {
        S << '\n';
        printIndentation();
        visitStmt(*I);
    }
    dedent();
    S << '>';
}

void StmtDumper::visitBlockStmt(BlockStmt *node)
{
    printHeader(node);

    if (node->hasLabel())
        S << llvm::format(" '%s'", node->getLabel()->getString());

    if (node->countDecls()) {
        S << '\n';
        printIndentation();
        S << "<declare";
        indent();
        for (BlockStmt::DeclIter I = node->beginDecls();
             I != node->endDecls(); ++I) {
            S << '\n';
            printIndentation();
            dumpAST(*I);
        }
        dedent();
        S << '>';
    }

    indent();
    for (StmtSequence::StmtIter I = node->beginStatements();
         I != node->endStatements(); ++I) {
        S << '\n';
        printIndentation();
        visitStmt(*I);
    }
    dedent();
    S << '>';
}

void StmtDumper::visitProcedureCallStmt(ProcedureCallStmt *node)
{
    printHeader(node)
        << llvm::format(" '%s'", node->getConnective()->getString());

    unsigned numArgs = node->getNumArgs();
    unsigned index = 0;
    indent();
    while (index < numArgs) {
        S << '\n';
        printIndentation();
        dumpAST(node->getArg(index));
        if (++index < numArgs)
            S << "; ";
    }
    dedent();
    S << '>';
}

void StmtDumper::visitReturnStmt(ReturnStmt *node)
{
    printHeader(node);
    if (node->hasReturnExpr()) {
        S << '\n';
        indent();
        printIndentation();
        dumpAST(node->getReturnExpr());
        dedent();
    }
    S << '>';
}

void StmtDumper::visitAssignmentStmt(AssignmentStmt *node)
{
    printHeader(node) << '\n';
    indent();
    printIndentation();
    dumpAST(node->getTarget()) << '\n';
    printIndentation();
    dumpAST(node->getAssignedExpr());
    dedent();
    S << '>';
}

void StmtDumper::visitIfStmt(IfStmt *node)
{
    printHeader(node) << '\n';
    indent();
    printIndentation();
    dumpAST(node->getCondition()) << '\n';
    printIndentation();
    visitStmtSequence(node->getConsequent());

    for (IfStmt::iterator I = node->beginElsif();
         I != node->endElsif(); ++I) {
        // For each elsif, print a condition and consequent group.
        S << '\n';
        printIndentation() << "<elsif\n";
        indent();
        printIndentation();
        dumpAST(I->getCondition()) << '\n';
        printIndentation();
        visitStmtSequence(I->getConsequent());
        dedent();
        S << '>';
    }

    if (node->hasAlternate()) {
        S << '\n';
        printIndentation() << "<else\n";
        indent();
        printIndentation();
        visitStmtSequence(node->getAlternate());
        dedent();
        S << '>';
    }

    dedent();
    S << '>';
}

void StmtDumper::visitWhileStmt(WhileStmt *node)
{
    printHeader(node) << '>';
}
