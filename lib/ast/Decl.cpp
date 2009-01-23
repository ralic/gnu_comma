//===-- ast/Decl.cpp ------------------------------------------ -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "comma/ast/Decl.h"
#include "comma/ast/AstRewriter.h"
#include <algorithm>

using namespace comma;

//===----------------------------------------------------------------------===//
// DeclarativeRegion

Decl *DeclarativeRegion::findDecl(IdentifierInfo *name, Type *type)
{
    DeclRange range = findDecls(name);
    for (DeclIter iter = range.first; iter != range.second; ++iter) {
        Decl *decl = iter->second;
        Type *candidateType = decl->getType();
        if (candidateType->equals(type))
            return decl;
    }
    return 0;
}

//===----------------------------------------------------------------------===//
// ModelDecl
ModelDecl::ModelDecl(AstKind kind, IdentifierInfo *percentId)
    : Decl(kind),
      location()
{
    assert(std::strcmp(percentId->getString(), "%") == 0 &&
           "Percent IdInfo not == \"%\"!");
    percent = DomainType::getPercent(percentId, this);
}

ModelDecl::ModelDecl(AstKind         kind,
                     IdentifierInfo *percentId,
                     IdentifierInfo *info,
                     const Location &loc)
    : Decl(kind, info),
      location(loc)
{
    assert(std::strcmp(percentId->getString(), "%") == 0 &&
           "Percent IdInfo not == \"%\"!");
    percent = DomainType::getPercent(percentId, this);
}

//===----------------------------------------------------------------------===//
// Sigoid

void Sigoid::addSupersignature(SignatureType *supersignature)
{
    if (directSupers.insert(supersignature)) {
        Sigoid *superDecl = supersignature->getDeclaration();
        AstRewriter rewriter;

        // Rewrite the percent node of the super signature to that of this
        // signature.
        rewriter.addRewrite(superDecl->getPercent(), getPercent());

        // If the supplied super signature is parameterized, install rewrites
        // mapping the formal parameters of the signature to the actual
        // parameters of the type.
        if (VarietyDecl *variety = superDecl->getVariety()) {
            unsigned arity = variety->getArity();
            for (unsigned i = 0; i < arity; ++i) {
                DomainType *formal = variety->getFormalDomain(i);
                DomainType *actual = supersignature->getActualParameter(i);
                rewriter.addRewrite(formal, actual);
            }
        }

        sig_iterator iter = superDecl->beginSupers();
        sig_iterator endIter = superDecl->endSupers();
        for ( ; iter != endIter; ++iter) {
            SignatureType *rewrite = rewriter.rewrite(*iter);
            supersignatures.insert(rewrite);
        }
        supersignatures.insert(supersignature);
    }
}

void Sigoid::addComponent(FunctionDecl *fdecl)
{
    IdentifierInfo *name = fdecl->getIdInfo();
    components.insert(ComponentTable::value_type(name, fdecl));
}

FunctionDecl *Sigoid::findComponent(IdentifierInfo *name,
                                    FunctionType *ftype)
{
    if (FunctionDecl *fdecl = findDirectComponent(name, ftype))
        return fdecl;

    ComponentRange range = findComponents(name);
    for (ComponentIter iter = range.first; iter != range.second; ++iter) {
        FunctionDecl *fdecl = iter->second;
        FunctionType *candidateType = fdecl->getType();
        if (candidateType->equals(ftype))
            return fdecl;
    }
    return 0;
}

FunctionDecl *Sigoid::findDirectComponent(IdentifierInfo *name,
                                          FunctionType *ftype)
{
    ComponentRange range = findComponents(name);
    for (ComponentIter iter = range.first; iter != range.second; ++iter) {
        FunctionDecl *fdecl = iter->second;
        FunctionType *candidateType = fdecl->getType();
        if (fdecl->isDeclarativeRegion(this) && candidateType->equals(ftype))
            return fdecl;
    }
    return 0;
}

bool Sigoid::removeComponent(FunctionDecl *fdecl)
{
    IdentifierInfo *name = fdecl->getIdInfo();
    ComponentRange range = findComponents(name);
    for (ComponentIter iter = range.first; iter != range.second; ++iter)
        if (iter->second == fdecl) {
            components.erase(iter);
            return true;
        }
    return false;
}

//===----------------------------------------------------------------------===//
// SignatureDecl
SignatureDecl::SignatureDecl(IdentifierInfo *percentId)
    : Sigoid(AST_SignatureDecl, percentId)
{
    canonicalType = new SignatureType(this);
}

SignatureDecl::SignatureDecl(IdentifierInfo *percentId,
                             IdentifierInfo *info,
                             const Location &loc)
    : Sigoid(AST_SignatureDecl, percentId, info, loc)
{
    canonicalType = new SignatureType(this);
}

//===----------------------------------------------------------------------===//
// VarietyDecl

VarietyDecl::VarietyDecl(IdentifierInfo  *percentId,
                         DomainType     **formals,
                         unsigned         arity)
    : Sigoid(AST_VarietyDecl, percentId)
{
    varietyType = new VarietyType(formals, this, arity);
}

VarietyDecl::VarietyDecl(IdentifierInfo *percentId,
                         IdentifierInfo *name,
                         Location        loc,
                         DomainType    **formals,
                         unsigned        arity)
    : Sigoid(AST_VarietyDecl, percentId, name, loc)
{
    varietyType = new VarietyType(formals, this, arity);
}

SignatureType *
VarietyDecl::getCorrespondingType(DomainType **args, unsigned numArgs)
{
    llvm::FoldingSetNodeID id;
    void *insertPos = 0;
    SignatureType *type;

    SignatureType::Profile(id, args, numArgs);
    type = types.FindNodeOrInsertPos(id, insertPos);
    if (type) return type;

    type = new SignatureType(this, args, numArgs);
    types.InsertNode(type, insertPos);
    return type;
}

SignatureType *VarietyDecl::getCorrespondingType()
{
    VarietyType *thisType = getType();
    DomainType **formals = reinterpret_cast<DomainType**>(thisType->formals);
    return getCorrespondingType(formals, getArity());
}

//===----------------------------------------------------------------------===//
// Domoid

Domoid::Domoid(AstKind         kind,
               IdentifierInfo *percentId,
               IdentifierInfo *idInfo,
               Location        loc)
    : ModelDecl(kind, percentId, idInfo, loc)
{
    principleSignature = new SignatureDecl(percentId);
}

//===----------------------------------------------------------------------===//
// DomainDecl
DomainDecl::DomainDecl(IdentifierInfo *percentId,
                       IdentifierInfo *name,
                       const Location &loc)
    : Domoid(AST_DomainDecl, percentId, name, loc)
{
    canonicalType = new DomainType(this);
}

DomainDecl::DomainDecl(AstKind         kind,
                       IdentifierInfo *percentId,
                       IdentifierInfo *info,
                       Location        loc)
    : Domoid(kind, percentId, info, loc)
{
    canonicalType = new DomainType(this);
}

//===----------------------------------------------------------------------===//
// FunctorDecl

FunctorDecl::FunctorDecl(IdentifierInfo *percentId,
                         IdentifierInfo *name,
                         Location        loc,
                         DomainType    **formals,
                         unsigned        arity)
    : Domoid(AST_FunctorDecl, percentId, name, loc)
{
    // NOTE: FunctorDecl passes ownership of the formal domain nodes to the
    // VarietyDecl created below.
    functor = new FunctorType(formals, this, arity);
}

DomainType *
FunctorDecl::getCorrespondingType(DomainType **args, unsigned numArgs)
{
    llvm::FoldingSetNodeID id;
    void *insertPos = 0;
    DomainType *type;

    DomainType::Profile(id, args, numArgs);
    type = types.FindNodeOrInsertPos(id, insertPos);
    if (type) return type;

    type = new DomainType(this, args, numArgs);
    types.InsertNode(type, insertPos);
    return type;
}

//===----------------------------------------------------------------------===//
// AbstractDomainDecl
AbstractDomainDecl::AbstractDomainDecl(IdentifierInfo *name,
                                       SignatureType  *type,
                                       Location        loc)
    : Domoid(AST_AbstractDomainDecl,
             type->getDeclaration()->getPercent()->getIdInfo(), name, loc),
      signature(type)
{
    abstractType = new DomainType(this);
}

//===----------------------------------------------------------------------===//
// FunctionDecl

FunctionDecl::FunctionDecl(IdentifierInfo    *name,
                           FunctionType      *type,
                           DeclarativeRegion *context,
                           Location           loc)
    : Decl(AST_FunctionDecl, name),
      ftype(type),
      context(context),
      location(loc)
{
}
