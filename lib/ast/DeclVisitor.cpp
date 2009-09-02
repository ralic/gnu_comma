//===-- ast/DeclVisitor.cpp ----------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "comma/ast/Decl.h"
#include "comma/ast/DeclVisitor.h"

using namespace comma;

using llvm::dyn_cast;
using llvm::cast;
using llvm::isa;

//===----------------------------------------------------------------------===//
// Virtual inner node visitors.
//===----------------------------------------------------------------------===//

void DeclVisitor::visitAst(Ast *node)
{
    if (Decl *decl = dyn_cast<Decl>(node))
        visitDecl(decl);
    else if (OverloadedDeclName *declName = dyn_cast<OverloadedDeclName>(node))
        visitOverloadedDeclName(declName);
}

void DeclVisitor::visitDecl(Decl *node)
{
    if (ModelDecl *model = dyn_cast<ModelDecl>(node))
        visitModelDecl(model);
    else if (SubroutineDecl *routine = dyn_cast<SubroutineDecl>(node))
        visitSubroutineDecl(routine);
    else if (TypeDecl *typed = dyn_cast<TypeDecl>(node))
        visitTypeDecl(typed);
    else
        assert(false && "Cannot visit this kind of node!");
}

void DeclVisitor::visitModelDecl(ModelDecl *node)
{
    if (Domoid *domoid = dyn_cast<Domoid>(node))
        visitDomoid(domoid);
    else if (Sigoid *sigoid = dyn_cast<Sigoid>(node))
        visitSigoid(sigoid);
    else
        assert(false && "Cannot visit this kind of node!");
}

void DeclVisitor::visitSigoid(Sigoid *node)
{
    if (SignatureDecl *sig = dyn_cast<SignatureDecl>(node))
        visitSignatureDecl(sig);
    else if (VarietyDecl *variety = dyn_cast<VarietyDecl>(node))
        visitVarietyDecl(variety);
    else
        assert(false && "Cannot visit this kind of node!");
}

void DeclVisitor::visitDomoid(Domoid *node)
{
    if (DomainDecl *domain = dyn_cast<DomainDecl>(node))
        visitDomainDecl(domain);
    else if (FunctorDecl *functor = dyn_cast<FunctorDecl>(functor))
        visitFunctorDecl(functor);
    else
        assert(false && "Cannot visit this kind of node!");
}

void DeclVisitor::visitSubroutineDecl(SubroutineDecl *node)
{
    if (FunctionDecl *function = dyn_cast<FunctionDecl>(node))
        visitFunctionDecl(function);
    else if (ProcedureDecl *procedure = dyn_cast<ProcedureDecl>(node))
        visitProcedureDecl(procedure);
    else
        assert(false && "Cannot visit this kind of node!");
}

void DeclVisitor::visitTypeDecl(TypeDecl *node)
{
    if (ValueDecl *value = dyn_cast<ValueDecl>(node))
        visitValueDecl(value);
    else if (CarrierDecl *carrier = dyn_cast<CarrierDecl>(node))
        visitCarrierDecl(carrier);
    else if (EnumerationDecl *enumeration = dyn_cast<EnumerationDecl>(node))
        visitEnumerationDecl(enumeration);
    else if (IntegerDecl *integer = dyn_cast<IntegerDecl>(node))
        visitIntegerDecl(integer);
    else
        assert(false && "Cannot visit this kind of node!");
}

void DeclVisitor::visitValueDecl(ValueDecl *node)
{
    if (DomainValueDecl *domainValue = dyn_cast<DomainValueDecl>(node))
        visitDomainValueDecl(domainValue);
    else if (ParamValueDecl *paramValue = dyn_cast<ParamValueDecl>(node))
        visitParamValueDecl(paramValue);
    else if (ObjectDecl *object = dyn_cast<ObjectDecl>(node))
        visitObjectDecl(object);
    else
        assert(false && "Cannot visit this kind of node!");
}

//===----------------------------------------------------------------------===//
// Concrete inner node visitors.
//===----------------------------------------------------------------------===//

void DeclVisitor::visitFunctionDecl(FunctionDecl *node)
{
    if (EnumLiteral *enumLit = dyn_cast<EnumLiteral>(node))
        visitEnumLiteral(enumLit);
}

//===----------------------------------------------------------------------===//
// Leaf visitors.
//===----------------------------------------------------------------------===//

void DeclVisitor::visitOverloadedDeclName(OverloadedDeclName *node) { }
void DeclVisitor::visitImportDecl(ImportDecl *node) { }
void DeclVisitor::visitSignatureDecl(SignatureDecl *node) { }
void DeclVisitor::visitVarietyDecl(VarietyDecl *node) { }
void DeclVisitor::visitAddDecl(AddDecl *node) { }
void DeclVisitor::visitDomainDecl(DomainDecl *node) { }
void DeclVisitor::visitFunctorDecl(FunctorDecl *node) { }
void DeclVisitor::visitProcedureDecl(ProcedureDecl *node) { }
void DeclVisitor::visitCarrierDecl(CarrierDecl *node) { }
void DeclVisitor::visitDomainValueDecl(DomainValueDecl *node) { }
void DeclVisitor::visitAbstractDomainDecl(AbstractDomainDecl *node) { }
void DeclVisitor::visitDomainInstanceDecl(DomainInstanceDecl *node) { }
void DeclVisitor::visitParamValueDecl(ParamValueDecl *node) { }
void DeclVisitor::visitObjectDecl(ObjectDecl *node) { }
void DeclVisitor::visitEnumLiteral(EnumLiteral *node) { }
void DeclVisitor::visitEnumerationDecl(EnumerationDecl *node) { }
void DeclVisitor::visitIntegerDecl(IntegerDecl *node) { }
