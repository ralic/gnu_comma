//===-- ast/DeclDumper.cpp ------------------------------------ -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "DeclDumper.h"
#include "comma/ast/Decl.h"
#include "comma/ast/Expr.h"
#include "comma/ast/Stmt.h"

#include "llvm/Support/Format.h"

using namespace comma;

using llvm::dyn_cast;
using llvm::cast;
using llvm::isa;

llvm::raw_ostream &DeclDumper::dump(Decl *decl, unsigned level)
{
    unsigned savedLevel = indentLevel;
    indentLevel = level;
    visitDecl(decl);
    indentLevel = savedLevel;
    return S;
}

llvm::raw_ostream &DeclDumper::printHeader(Ast *node)
{
    const char *nameString;

    // OverloadedDeclName is not a Decl, just a helper class, so we have to jump
    // thru a hoop to get at the name of such a node.
    if (Decl *decl = dyn_cast<Decl>(node))
        nameString = decl->getString();
    else
        nameString = cast<OverloadedDeclName>(node)->getString();

    AstDumperBase::printHeader(node) << llvm::format(" '%s'", nameString);
    return S;
}

//===----------------------------------------------------------------------===//
// Visitor implementations.

void DeclDumper::visitOverloadedDeclName(OverloadedDeclName *node)
{
    printHeader(node);
    indent();
    for (unsigned i = 0; i < node->numOverloads(); ++i) {
        S << '\n';
        printIndentation();
        DeclVisitor::visitSubroutineDecl(node->getOverload(i));
    }
    dedent();
    S << '>';
}

void DeclDumper::visitImportDecl(ImportDecl *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitSignatureDecl(SignatureDecl *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitVarietyDecl(VarietyDecl *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitSigInstanceDecl(SigInstanceDecl *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitAddDecl(AddDecl *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitDomainDecl(DomainDecl *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitFunctorDecl(FunctorDecl *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitSubroutineDecl(SubroutineDecl *node)
{
    printHeader(node) << '\n';
    indent();
    printIndentation();
    dumpAST(node->getType());
    if (node->hasBody()) {
        S << '\n';
        printIndentation();
        dumpAST(node->getBody());
    }
    dedent();
    S << '>';
}

void DeclDumper::visitFunctionDecl(FunctionDecl *node)
{
    visitSubroutineDecl(node);
}

void DeclDumper::visitProcedureDecl(ProcedureDecl *node)
{
    visitSubroutineDecl(node);
}

void DeclDumper::visitCarrierDecl(CarrierDecl *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitDomainTypeDecl(DomainTypeDecl *node)
{
    if (AbstractDomainDecl *abstract = dyn_cast<AbstractDomainDecl>(node))
        visitAbstractDomainDecl(abstract);
    else {
        DomainInstanceDecl *instance = cast<DomainInstanceDecl>(node);
        visitDomainInstanceDecl(instance);
    }
}

void DeclDumper::visitAbstractDomainDecl(AbstractDomainDecl *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitDomainInstanceDecl(DomainInstanceDecl *node)
{
    printHeader(node);

    indent();
    for (unsigned i = 0; i < node->getArity(); ++i) {
        S << '\n';
        printIndentation();
        visitDomainTypeDecl(node->getActualParam(i));
    }
    dedent();
    S << '>';
}

void DeclDumper::visitParamValueDecl(ParamValueDecl *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitObjectDecl(ObjectDecl *node)
{
    printHeader(node);
    if (node->hasInitializer()) {
        S << '\n';
        indent();
        printIndentation();
        dumpAST(node->getInitializer());
        dedent();
    }
    S << '>';
}

void DeclDumper::visitEnumLiteral(EnumLiteral *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitEnumerationDecl(EnumerationDecl *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitIntegerDecl(IntegerDecl *node)
{
    printHeader(node) << '>';
}

void DeclDumper::visitArrayDecl(ArrayDecl *node)
{
    printHeader(node) << '>';
}


