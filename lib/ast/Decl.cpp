//===-- ast/Decl.cpp ------------------------------------------ -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008-2010, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "comma/ast/AstResource.h"
#include "comma/ast/AttribExpr.h"
#include "comma/ast/Decl.h"
#include "comma/ast/DeclRewriter.h"
#include "comma/ast/DSTDefinition.h"
#include "comma/ast/KeywordSelector.h"
#include "comma/ast/Stmt.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <cstring>
#include <iostream>

using namespace comma;
using llvm::dyn_cast;
using llvm::cast;
using llvm::isa;

//===----------------------------------------------------------------------===//
// Decl

DeclRegion *Decl::asDeclRegion()
{
    switch (getKind()) {
    default:
        return 0;
    case AST_DomainInstanceDecl:
        return static_cast<DomainInstanceDecl*>(this);
    case AST_AbstractDomainDecl:
        return static_cast<AbstractDomainDecl*>(this);
    case AST_PercentDecl:
        return static_cast<PercentDecl*>(this);
    case AST_EnumerationDecl:
        return static_cast<EnumerationDecl*>(this);
    case AST_AddDecl:
        return static_cast<AddDecl*>(this);
    case AST_FunctionDecl:
        return static_cast<FunctionDecl*>(this);
    case AST_ProcedureDecl:
        return static_cast<ProcedureDecl*>(this);
    case AST_IntegerDecl:
        return static_cast<IntegerDecl*>(this);
    case AST_AccessDecl:
        return static_cast<AccessDecl*>(this);
    }
}

Decl *Decl::resolveOrigin()
{
    Decl *res = this;

    while (res->hasOrigin())
        res = res->getOrigin();

    return res;
}

//===----------------------------------------------------------------------===//
// ModelDecl

ModelDecl::ModelDecl(AstResource &resource,
                     AstKind kind, IdentifierInfo *name, Location loc)
    : Decl(kind, name, loc),
      percent(0),
      resource(resource)
{
    percent = new PercentDecl(resource, this);
}

ModelDecl::~ModelDecl()
{
    delete percent;
}

const SignatureSet &ModelDecl::getSignatureSet() const
{
    return percent->getSignatureSet();
}

bool ModelDecl::addDirectSignature(SigInstanceDecl *signature)
{
    // Rewrite % nodes of the signature to the % nodes of this model and map any
    // formal arguments to the actuals.
    AstRewriter rewrites(resource);
    rewrites.addTypeRewrite(signature->getSigoid()->getPercentType(),
                            getPercentType());
    rewrites.installRewrites(signature);
    return percent->sigset.addDirectSignature(signature, rewrites);
}

unsigned ModelDecl::getArity() const
{
    return 0;
}

/// Returns the abstract domain declaration corresponding the i'th formal
/// parameter.  This method will assert if this declaration is not
/// parameterized.
AbstractDomainDecl *ModelDecl::getFormalDecl(unsigned i)
{
    assert(!isParameterized() &&
           "Parameterized decls must implement this method!");
    assert(false &&
           "Cannot retrieve formal decls from a non-parameterized model!");
    return 0;
}

/// Returns the index of the given AbstractDomainDecl which must be a formal
/// parameter of this model.  This method will assert if this declaration is not
/// parameterized.
unsigned ModelDecl::getFormalIndex(const AbstractDomainDecl *ADDecl) const
{
    assert(isParameterized() &&
           "Cannot retrieve formal index from a non-parameterized model!");
    unsigned arity = getArity();
    for (unsigned i = 0; i < arity; ++i) {
        const AbstractDomainDecl *candidate = getFormalDecl(i);
        if (candidate == ADDecl)
            return i;
    }
    assert(false && "Not a formal parameter decl!");
    return -1U;
}

DomainType *ModelDecl::getFormalType(unsigned i)
{
    assert(isParameterized() &&
           "Cannot retrieve formal type from a non-parameterized model!");
    return getFormalDecl(i)->getType();
}

/// Returns the SigInstanceDecl which the i'th actual parameter must satisfy.
/// This method will assert if this declaration is not parameterized.
SigInstanceDecl *ModelDecl::getFormalSignature(unsigned i) const
{
    assert(isParameterized() &&
           "Cannot retrieve formal signature from a non-parameterized model!");
    return getFormalDecl(i)->getPrincipleSignature();
}

/// Returns the IdentifierInfo which labels the i'th formal parameter.  This
/// method will assert if this declaration is not parameterized.
IdentifierInfo *ModelDecl::getFormalIdInfo(unsigned i) const
{
    assert(isParameterized() &&
           "Cannot retrieve formal identifier from a non-parameterized model!");
    return getFormalDecl(i)->getIdInfo();
}

/// Returns the index of the parameter corresponding to the given keyword,
/// or -1 if no such keyword exists.  This method will assert if this
/// declaration is not parameterized.
int ModelDecl::getKeywordIndex(IdentifierInfo *keyword) const
{
    assert(isParameterized() &&
           "Cannot retrieve keyword index from a non-parameterized model!");
    for (unsigned i = 0; i < getArity(); ++i)
        if (getFormalDecl(i)->getIdInfo() == keyword)
            return i;
    return -1;
}

//===----------------------------------------------------------------------===//
// SignatureDecl
SignatureDecl::SignatureDecl(AstResource &resource,
                             IdentifierInfo *info, const Location &loc)
    : Sigoid(resource, AST_SignatureDecl, info, loc)
{
    theInstance = new SigInstanceDecl(this);
}

//===----------------------------------------------------------------------===//
// VarietyDecl

VarietyDecl::VarietyDecl(AstResource &resource,
                         IdentifierInfo *name, Location loc,
                         AbstractDomainDecl **formals, unsigned arity)
    : Sigoid(resource, AST_VarietyDecl, name, loc),
      arity(arity)
{
    formalDecls = new AbstractDomainDecl*[arity];
    std::copy(formals, formals + arity, formalDecls);
}

SigInstanceDecl *
VarietyDecl::getInstance(DomainTypeDecl **args, unsigned numArgs)
{
    llvm::FoldingSetNodeID id;
    void *insertPos = 0;
    SigInstanceDecl *instance;

    SigInstanceDecl::Profile(id, args, numArgs);
    instance = instances.FindNodeOrInsertPos(id, insertPos);
    if (instance)
        return instance;

    instance = new SigInstanceDecl(this, args, numArgs);
    instances.InsertNode(instance, insertPos);
    return instance;
}

//===----------------------------------------------------------------------===//
// Domoid

Domoid::Domoid(AstResource &resource,
               AstKind kind, IdentifierInfo *idInfo, Location loc)
    : ModelDecl(resource, kind, idInfo, loc) { }

//===----------------------------------------------------------------------===//
// AddDecl

AddDecl::AddDecl(PercentDecl *percent)
    : Decl(AST_AddDecl),
      DeclRegion(AST_AddDecl, percent),
      carrier(0) { }

AddDecl::AddDecl(DomainInstanceDecl *instance)
    : Decl(AST_AddDecl),
      DeclRegion(AST_AddDecl, instance),
      carrier(0) { }

Domoid *AddDecl::getImplementedDomoid()
{
    Ast *parent = getParent()->asAst();

    if (PercentDecl *percent = dyn_cast<PercentDecl>(parent))
        return cast<Domoid>(percent->getDefinition());
    else {
        DomainInstanceDecl *instance = cast<DomainInstanceDecl>(parent);
        return instance->getDefinition();
    }
}

DomainDecl *AddDecl::getImplementedDomain()
{
    return dyn_cast<DomainDecl>(getImplementedDomoid());
}

FunctorDecl *AddDecl::getImplementedFunctor()
{
    return dyn_cast<FunctorDecl>(getImplementedDomoid());
}

bool AddDecl::implementsDomain() const
{
    return getImplementedDomain() != 0;
}

bool AddDecl::implementsFunctor() const
{
    return getImplementedFunctor() != 0;
}

//===----------------------------------------------------------------------===//
// DomainDecl

DomainDecl::DomainDecl(AstResource &resource,
                       IdentifierInfo *name, const Location &loc)
    : Domoid(resource, AST_DomainDecl, name, loc),
      instance(0)
{
    implementation = new AddDecl(getPercent());
}

// FIXME: This is a temporary solution to the problem of initializing domain
// instances before the corresponding DomDecl is fully initialized.  In
// particular, we need a machanism similar to the observer failcility in
// DeclRegion but for signature sets.
DomainInstanceDecl *DomainDecl::getInstance()
{
    if (instance == 0)
        instance = new DomainInstanceDecl(getAstResource(), this);
    return instance;
}

void DomainDecl::finalize()
{
    // Construct (if needed) and notify our one and only instance that this
    // domain is finished.
    getInstance()->finalize();

    // Mark this domain a finalized.
    bits = 1;
}

bool DomainDecl::isFinalized() const
{
    return bits == 1;
}

//===----------------------------------------------------------------------===//
// FunctorDecl

FunctorDecl::FunctorDecl(AstResource &resource,
                         IdentifierInfo *name, Location loc,
                         AbstractDomainDecl **formals, unsigned arity)
    : Domoid(resource, AST_FunctorDecl, name, loc),
      arity(arity)
{
    assert(arity && "Cannot construct functors with no arguments!");

    formalDecls = new AbstractDomainDecl*[arity];
    std::copy(formals, formals + arity, formalDecls);
    implementation = new AddDecl(getPercent());
}

DomainInstanceDecl *
FunctorDecl::getInstance(DomainTypeDecl **args, unsigned numArgs)
{
    llvm::FoldingSetNodeID id;
    void *insertPos = 0;
    DomainInstanceDecl *instance;

    DomainInstanceDecl::Profile(id, args, numArgs);
    instance = instances.FindNodeOrInsertPos(id, insertPos);
    if (instance) return instance;

    instance = new DomainInstanceDecl(getAstResource(), this, args, numArgs);
    instances.InsertNode(instance, insertPos);
    return instance;
}


void FunctorDecl::finalize()
{
    // Notify any instances that this functor is finished.
    InstanceSet::iterator I = instances.begin();
    InstanceSet::iterator E = instances.end();
    for ( ; I != E; ++I)
        I->finalize();

    // Mark this functor as finalized.
    bits = 1;
}

bool FunctorDecl::isFinalized() const
{
    return bits == 1;
}

//===----------------------------------------------------------------------===//
// SigInstanceDecl

SigInstanceDecl::SigInstanceDecl(SignatureDecl *decl)
    : Decl(AST_SigInstanceDecl, decl->getIdInfo()),
      underlyingSigoid(decl),
      arguments(0)
{ }

SigInstanceDecl::SigInstanceDecl(VarietyDecl *decl,
                                 DomainTypeDecl **args, unsigned numArgs)
    : Decl(AST_SigInstanceDecl, decl->getIdInfo()),
      underlyingSigoid(decl)
{
    assert(numArgs && "No arguments given to parameterized instance!");
    arguments = new DomainTypeDecl*[numArgs];
    std::copy(args, args + numArgs, arguments);
}

SignatureDecl *SigInstanceDecl::getSignature() const
{
    return dyn_cast<SignatureDecl>(underlyingSigoid);
}

VarietyDecl *SigInstanceDecl::getVariety() const
{
    return dyn_cast<VarietyDecl>(underlyingSigoid);
}

unsigned SigInstanceDecl::getArity() const
{
    VarietyDecl *variety = getVariety();
    if (variety)
        return variety->getArity();
    return 0;
}

void SigInstanceDecl::Profile(llvm::FoldingSetNodeID &ID,
                              DomainTypeDecl **args, unsigned numArgs)
{
    if (numArgs == 0)
        ID.AddPointer(0);
    else {
        for (unsigned i = 0; i < numArgs; ++i)
            ID.AddPointer(args[i]);
    }
}

//===----------------------------------------------------------------------===//
// SubroutineDecl

SubroutineDecl::SubroutineDecl(AstKind kind, IdentifierInfo *name, Location loc,
                               ParamValueDecl **params, unsigned numParams,
                               DeclRegion *parent)
    : Decl(kind, name, loc, parent),
      DeclRegion(kind, parent),
      opID(PO::NotPrimitive),
      numParameters(numParams),
      parameters(0),
      body(0),
      declarationLink(0, FORWARD_TAG)
{
    assert(this->denotesSubroutineDecl());

    if (numParams > 0)
        parameters = new ParamValueDecl*[numParams];
    llvm::SmallVector<const Type*, 8> paramTypes;

    for (unsigned i = 0; i < numParams; ++i) {
        ParamValueDecl *paramDecl = params[i];
        parameters[i] = paramDecl;
        paramTypes.push_back(paramDecl->getType());
    }
}

SubroutineDecl::SubroutineDecl(AstKind kind, IdentifierInfo *name, Location loc,
                               IdentifierInfo **keywords, SubroutineType *type,
                               DeclRegion *parent)
    : Decl(kind, name, loc, parent),
      DeclRegion(kind, parent),
      opID(PO::NotPrimitive),
      numParameters(type->getArity()),
      parameters(0),
      body(0),
      declarationLink(0, FORWARD_TAG)
{
    assert(this->denotesSubroutineDecl());

    if (numParameters == 0)
        return;

    parameters = new ParamValueDecl*[numParameters];
    for (unsigned i = 0; i < numParameters; ++i) {
        Type *paramType = type->getArgType(i);
        ParamValueDecl *param =
            new ParamValueDecl(keywords[i], paramType, PM::MODE_DEFAULT, 0);
        parameters[i] = param;
    }
}

SubroutineDecl::SubroutineDecl(AstKind kind, IdentifierInfo *name, Location loc,
                               DeclRegion *parent)
    : Decl(kind, name, loc, parent),
      DeclRegion(kind, parent),
      opID(PO::NotPrimitive),
      numParameters(0),
      parameters(0),
      body(0),
      declarationLink(0, FORWARD_TAG)
{
    assert(this->denotesSubroutineDecl());
}

SubroutineDecl::~SubroutineDecl()
{
    if (parameters) {
        for (unsigned i = 0; i < numParameters; ++i)
            delete parameters[i];
        delete[] parameters;
    }
}

int SubroutineDecl::getKeywordIndex(IdentifierInfo *key) const
{
    for (unsigned i = 0; i < getArity(); ++i) {
        if (parameters[i]->getIdInfo() == key)
            return i;
    }
    return -1;
}

int SubroutineDecl::getKeywordIndex(KeywordSelector *key) const
{
    return getKeywordIndex(key->getKeyword());
}

bool SubroutineDecl::keywordsMatch(const SubroutineDecl *SRDecl) const
{
    unsigned arity = getArity();
    if (SRDecl->getArity() == arity) {
        for (unsigned i = 0; i < arity; ++i)
            if (getParamKeyword(i) != SRDecl->getParamKeyword(i))
                return false;
        return true;
    }
    return false;
}

bool SubroutineDecl::paramModesMatch(const SubroutineDecl *SRDecl) const
{
    unsigned arity = getArity();
    if (SRDecl->getArity() == arity) {
        for (unsigned i = 0; i < arity; ++i)
            if (getParamMode(i) != SRDecl->getParamMode(i))
                return false;
        return true;
    }
    return false;
}

void SubroutineDecl::setDefiningDeclaration(SubroutineDecl *routineDecl)
{
    // Check that we are not reseting the link, and that the given subroutine if
    // of a compatible kind.
    assert(declarationLink.getPointer() == 0 && "Cannot reset base declaration!");
    assert(((isa<FunctionDecl>(this) && isa<FunctionDecl>(routineDecl)) ||
            (isa<ProcedureDecl>(this) && isa<ProcedureDecl>(routineDecl))) &&
           "Defining declarations must be of the same kind as the parent!");

    // Check that the defining declaration does not already have its link set.
    assert(routineDecl->declarationLink.getPointer() == 0);

    declarationLink.setPointer(routineDecl);
    declarationLink.setInt(DEFINITION_TAG);
    routineDecl->declarationLink.setPointer(this);
    routineDecl->declarationLink.setInt(FORWARD_TAG);
}

bool SubroutineDecl::hasBody() const
{
    return body || getDefiningDeclaration();
}

BlockStmt *SubroutineDecl::getBody()
{
    if (body)
        return body;

    if (SubroutineDecl *definition = getDefiningDeclaration())
        return definition->body;

    return 0;
}

const Pragma *SubroutineDecl::findPragma(pragma::PragmaID ID) const
{
    const_pragma_iterator I = begin_pragmas();
    const_pragma_iterator E = end_pragmas();
    for ( ; I != E; ++I) {
        if (I->getKind() == ID)
            return &*I;
    }
    return 0;
}

//===----------------------------------------------------------------------===//
// ProcedureDecl

ProcedureDecl::ProcedureDecl(AstResource &resource,
                             IdentifierInfo *name, Location loc,
                             ParamValueDecl **params, unsigned numParams,
                             DeclRegion *parent)
    : SubroutineDecl(AST_ProcedureDecl, name, loc,
                     params, numParams, parent)
{
    // Construct our type.
    llvm::SmallVector<Type*, 8> paramTypes;
    for (unsigned i = 0; i < numParams; ++i)
        paramTypes.push_back(params[i]->getType());
    correspondingType =
        resource.getProcedureType(paramTypes.data(), numParams);
}

//===----------------------------------------------------------------------===//
// FunctionDecl

FunctionDecl::FunctionDecl(AstResource &resource,
                           IdentifierInfo *name, Location loc,
                           ParamValueDecl **params, unsigned numParams,
                           Type *returnType, DeclRegion *parent)
    : SubroutineDecl(AST_FunctionDecl, name, loc,
                     params, numParams, parent)
{
    initializeCorrespondingType(resource, returnType);
}

FunctionDecl::FunctionDecl(AstKind kind, AstResource &resource,
                           IdentifierInfo *name, Location loc,
                           EnumerationType *returnType, DeclRegion *parent)
    : SubroutineDecl(kind, name, loc, parent)
{
    initializeCorrespondingType(resource, returnType);
}

void FunctionDecl::initializeCorrespondingType(AstResource &resource,
                                               Type *returnType)
{
    llvm::SmallVector<Type*, 8> paramTypes;
    for (unsigned i = 0; i < numParameters; ++i)
        paramTypes.push_back(parameters[i]->getType());
    correspondingType =
        resource.getFunctionType(paramTypes.data(), numParameters, returnType);
}

//===----------------------------------------------------------------------===//
// IncompleteTypeDecl

IncompleteTypeDecl::IncompleteTypeDecl(AstResource &resource,
                                       IdentifierInfo *name, Location loc,
                                       DeclRegion *region)
        : TypeDecl(AST_IncompleteTypeDecl, name, loc, region),
          completion(0)
{
    // Create the root and first subtype defined by this incomplete type
    // declaration.
    IncompleteType *rootType = resource.createIncompleteType(this);
    CorrespondingType = resource.createIncompleteSubtype(name, rootType);
}

bool IncompleteTypeDecl::isCompatibleCompletion(const TypeDecl *decl) const
{
    const DeclRegion *thisRegion = this->getDeclRegion();
    const DeclRegion *completion = decl->getDeclRegion();

    if (this->hasCompletion())
        return false;

    if (thisRegion == completion)
        return true;

    if (isa<PercentDecl>(thisRegion) && isa<AddDecl>(completion) &&
        thisRegion == completion->getParent())
        return true;

    return false;
}

bool IncompleteTypeDecl::completionIsVisibleIn(const DeclRegion *region) const
{
    if (!hasCompletion())
        return false;

    const DeclRegion *target = getDeclRegion();

    do {
        if (target == region)
            return true;
    } while ((region = region->getParent()));

    return false;
}

//===----------------------------------------------------------------------===//
// DomainTypeDecl

DomainTypeDecl::DomainTypeDecl(AstKind kind, AstResource &resource,
                               IdentifierInfo *name, Location loc)
    : TypeDecl(kind, name, loc),
      DeclRegion(kind)
{
    assert(this->denotesDomainTypeDecl());
    CorrespondingType = resource.createDomainType(this);
}

//===----------------------------------------------------------------------===//
// AbstractDomainDecl

AbstractDomainDecl::AbstractDomainDecl(AstResource &resource,
                                       IdentifierInfo *name, Location loc,
                                       SigInstanceDecl *sig)
    : DomainTypeDecl(AST_AbstractDomainDecl, resource, name, loc)
{
    Sigoid *sigoid = sig->getSigoid();
    PercentDecl *percent = sigoid->getPercent();
    DeclRewriter rewriter(sigoid->getAstResource(), this, percent);

    // Establish a mapping from the % node of the signature to the type of this
    // abstract domain.
    rewriter.addTypeRewrite(sigoid->getPercentType(), getType());

    // Establish mappings from the formal parameters of the signature to the
    // actual parameters of the given instance (this is a no-op if the signature
    // is not parametrized).
    rewriter.installRewrites(sig);

    sigset.addDirectSignature(sig, rewriter);
    addDeclarationsUsingRewrites(rewriter, percent);
}

//===----------------------------------------------------------------------===//
// DomainInstanceDecl
DomainInstanceDecl::DomainInstanceDecl(AstResource &resource,
                                       DomainDecl *domain)
    : DomainTypeDecl(AST_DomainInstanceDecl, resource, domain->getIdInfo()),
      definition(domain)
{
    PercentDecl *percent = domain->getPercent();

    // Ensure that we are notified if the declarations provided by the percent
    // node of the defining domoid change.
    percent->addObserver(this);

    // Initialize the available declarations.
    initializeInstance(domain);
}

DomainInstanceDecl::DomainInstanceDecl(AstResource &resource,
                                       FunctorDecl *functor,
                                       DomainTypeDecl **args, unsigned numArgs)
    : DomainTypeDecl(AST_DomainInstanceDecl, resource, functor->getIdInfo()),
      definition(functor)
{
    assert(functor->getArity() == numArgs &&
           "Wrong number of arguments for domain instance!");

    arguments = new DomainTypeDecl*[numArgs];
    std::copy(args, args + numArgs, arguments);

    // Ensure that we are notified if the declarations provided by percent
    // change.
    PercentDecl *percent = functor->getPercent();
    percent->addObserver(this);

    // Initialize the available declarations.
    initializeInstance(functor);
}

void DomainInstanceDecl::initializeInstance(Domoid *definition)
{
    AstResource &resource = definition->getAstResource();
    PercentDecl *percent = definition->getPercent();

    // Obtain a rewritten version of the public exports provided by percent.
    DeclRewriter *rewriter = new DeclRewriter(resource, this, percent);
    rewriter->addTypeRewrite(definition->getPercentType(), getType());
    rewriter->installRewrites(getType());
    addDeclarationsUsingRewrites(*rewriter, percent);

    // Populate our signature set with a rewritten version.
    const SignatureSet &SS = definition->getSignatureSet();
    for (SignatureSet::const_iterator I = SS.begin(); I != SS.end(); ++I)
        sigset.addDirectSignature(*I, *rewriter);

    // Initialize the body if the defining domoid is finalized.  Otherwise hold
    // onto the rewriter untill we are in a finalized state.
    if (definition->isFinalized()) {
        initializeBody(*rewriter);
        delete rewriter;
    }
    else {
        body = rewriter;
        representationType = 0;
    }
}

void DomainInstanceDecl::finalize()
{
    // If we have not yet initialized the body of this instance compute it now.
    if (body.is<AddDecl*>()) return;

    DeclRewriter *rewriter = body.get<DeclRewriter*>();
    initializeBody(*rewriter);
    delete rewriter;
}

void DomainInstanceDecl::initializeBody(DeclRewriter &rewriter)
{
    AddDecl *orig = definition->getImplementation();
    AddDecl *add = new AddDecl(this);
    rewriter.setContext(add, orig);

    // Iterate over the complete set of declarations provided by the body of our
    // defining domoid.
    add->addDeclarationsUsingRewrites(rewriter, orig);

    // FIXME: No need for the conditional when it is guaranteed that a finalized
    // domain defines a carrier.
    if (orig->hasCarrier()) {
        add->setCarrier(rewriter.rewriteCarrierDecl(orig->getCarrier()));

        // Resolve the representation type of this domain.
        PrimaryType *rep = add->getCarrier()->getType();

        if (DomainType *domain = dyn_cast<DomainType>(rep)) {
            DomainInstanceDecl *instance = domain->getInstanceDecl();
            rep = instance->getRepresentationType();
        }
        representationType = rep;
    }
    body = add;
}

bool DomainInstanceDecl::isDependent() const
{
    for (unsigned i = 0; i < getArity(); ++i) {
        const DomainType *param = cast<DomainType>(getActualParamType(i));
        if (param->isAbstract())
            return true;
        if (param->denotesPercent())
            return true;
        if (param->getInstanceDecl()->isDependent())
            return true;
    }
    return false;
}

unsigned DomainInstanceDecl::getArity() const
{
    if (getType()->denotesPercent())
        return 0;
    if (FunctorDecl *functor = dyn_cast<FunctorDecl>(definition))
        return functor->getArity();
    return 0;
}

void DomainInstanceDecl::notifyAddDecl(Decl *decl)
{
    AstResource &resource = getDefinition()->getAstResource();
    DeclRewriter rewriter(resource, this, decl->getDeclRegion());
    rewriter.addTypeRewrite(getDefinition()->getPercentType(), getType());
    rewriter.installRewrites(getType());
    addDeclarationUsingRewrites(rewriter, decl);
}

void DomainInstanceDecl::notifyRemoveDecl(Decl *decl)
{
    // FIXME:  Implement.
}

void DomainInstanceDecl::Profile(llvm::FoldingSetNodeID &id,
                                 DomainTypeDecl **args, unsigned numArgs)
{
    for (unsigned i = 0; i < numArgs; ++i)
        id.AddPointer(args[i]);
}

//===----------------------------------------------------------------------===//
// PercentDecl

PercentDecl::PercentDecl(AstResource &resource, ModelDecl *model)
    : DomainTypeDecl(AST_PercentDecl, resource,
                     resource.getIdentifierInfo("%"), model->getLocation()),
      underlyingModel(model) { }

//===----------------------------------------------------------------------===//
// ParamValueDecl

PM::ParameterMode ParamValueDecl::getExplicitParameterMode() const
{
    return static_cast<PM::ParameterMode>(bits);
}

void ParamValueDecl::setParameterMode(PM::ParameterMode mode)
{
    bits = mode;
}

bool ParamValueDecl::parameterModeSpecified() const
{
    return getExplicitParameterMode() != PM::MODE_DEFAULT;
}

PM::ParameterMode ParamValueDecl::getParameterMode() const
{
    PM::ParameterMode mode = getExplicitParameterMode();
    if (mode == PM::MODE_DEFAULT)
        return PM::MODE_IN;
    else
        return mode;
}

//===----------------------------------------------------------------------===//
// EnumLiteral
EnumLiteral::EnumLiteral(AstResource &resource,
                         IdentifierInfo *name, Location loc, unsigned index,
                         EnumerationType *type, EnumerationDecl *parent)
    : FunctionDecl(AST_EnumLiteral, resource, name, loc, type, parent),
      index(index)
{
    setAsPrimitive(PO::ENUM_op);
}

//===----------------------------------------------------------------------===//
// EnumerationDecl
EnumerationDecl::EnumerationDecl(AstResource &resource,
                                 IdentifierInfo *name, Location loc,
                                 std::pair<IdentifierInfo*, Location> *elems,
                                 unsigned numElems, DeclRegion *parent)
    : TypeDecl(AST_EnumerationDecl, name, loc, parent),
      DeclRegion(AST_EnumerationDecl, parent),
      numLiterals(numElems)
{
    // Build the root type corresponding to this declaration.
    EnumerationType *root = resource.createEnumType(this);

    // Now, we have a bootstrap issue here to contend with.  We need to perform
    // the following sequence of actions:
    //
    //    - Build the root type of this declaration.
    //
    //    - Build the first constrained subtype of the root type.
    //
    //    - Build the set of EnumLiteral's associated with this declaration,
    //      each of which has the first constrained subtype as type.
    //
    // However, in order to construct the first subtype we need the literals to
    // be available so that we may form the constraint, but we also need the
    // first subtype available to construct the literals.
    //
    // The solution to this circularity is to specify the constraint of the
    // first subtype as attribute expressions over the base subtype.  In source
    // code, the constraint would be similar to "E'Base'First .. E'Base'Last".
    // Note that these attributes are static expressions.
    EnumerationType *base = root->getBaseSubtype();
    Expr *lower = new FirstAE(base, 0);
    Expr *upper = new LastAE(base, 0);

    // Construct the subtype.
    EnumerationType *subtype;
    subtype = resource.createEnumSubtype(name, root, lower, upper);
    CorrespondingType = subtype;

    // Construct enumeration literals for each Id/Location pair and add them to
    // this decls declarative region.
    for (unsigned i = 0; i < numElems; ++i) {
        IdentifierInfo *name = elems[i].first;
        Location loc = elems[i].second;
        EnumLiteral *elem =
            new EnumLiteral(resource, name, loc, i, subtype, this);
        addDecl(elem);
    }
}

void EnumerationDecl::generateImplicitDeclarations(AstResource &resource)
{
    EnumerationType *type = getType();
    Location loc = getLocation();

    addDecl(resource.createPrimitiveDecl(PO::EQ_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::NE_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::LT_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::LE_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::GT_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::GE_op, loc, type, this));
}

void EnumerationDecl::generateBooleanDeclarations(AstResource &resource)
{
    // Boolean exports all of the operations a standard enumeration type does,
    // plus the logical operations "and", "or", and "not".
    generateImplicitDeclarations(resource);

    EnumerationType *type = getType();
    Location loc = getLocation();

    addDecl(resource.createPrimitiveDecl(PO::LNOT_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::LAND_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::LXOR_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::LOR_op, loc, type, this));
}

EnumLiteral *EnumerationDecl::findLiteral(IdentifierInfo *name)
{
    PredRange range = findDecls(name);

    if (range.first != range.second)
        return cast<EnumLiteral>(*range.first);
    return 0;
}

const EnumLiteral *EnumerationDecl::findCharacterLiteral(char ch) const
{
    char target[] = { '\'', ch, '\'', 0 };

    // Traverse the declarative region and do a case by case comparison of the
    // literal names and the target string.
    for (ConstDeclIter I = beginDecls(); I != endDecls(); ++I) {
        if (EnumLiteral *lit = dyn_cast<EnumLiteral>(*I)) {
            const char *name = lit->getIdInfo()->getString();
            if (strcmp(name, target) == 0)
                return lit;
        }
    }
    return 0;
}

const EnumLiteral *EnumerationDecl::getFirstLiteral() const
{
    return const_cast<EnumerationDecl*>(this)->getFirstLiteral();
}

EnumLiteral *EnumerationDecl::getFirstLiteral() {
    for (DeclIter I = beginDecls(); I != endDecls(); ++I) {
        if (EnumLiteral *lit = dyn_cast<EnumLiteral>(*I))
            return lit;
    }
    assert(false && "Enumeration decl does not contain any literals!");
    return 0;
}

const EnumLiteral *EnumerationDecl::getLastLiteral() const
{
    return const_cast<EnumerationDecl*>(this)->getLastLiteral();
}

EnumLiteral *EnumerationDecl::getLastLiteral()
{
    for (reverse_decl_iter I = rbegin_decls(); I != rend_decls(); ++I) {
        if (EnumLiteral *lit = dyn_cast<EnumLiteral>(*I))
            return lit;
    }
    assert(false && "Enumeration decl does not contain any literals!");
    return 0;
}

//===----------------------------------------------------------------------===//
// EnumSubtypeDecl

EnumSubtypeDecl::EnumSubtypeDecl(AstResource &resource, IdentifierInfo *name,
                                 Location loc,
                                 EnumerationType *subtype, DeclRegion *region)
    : SubtypeDecl(AST_EnumSubtypeDecl, name, loc, region)
{
    CorrespondingType = resource.createEnumSubtype(name, subtype);
}

EnumSubtypeDecl::EnumSubtypeDecl(AstResource &resource, IdentifierInfo *name,
                                 Location loc,
                                 EnumerationType *subtype,
                                 Expr *lower, Expr *upper, DeclRegion *region)
    : SubtypeDecl(AST_EnumSubtypeDecl, name, loc, region)
{
    CorrespondingType = resource.createEnumSubtype(name, subtype, lower, upper);
}

//===----------------------------------------------------------------------===//
// IntegerDecl

IntegerDecl::IntegerDecl(AstResource &resource,
                         IdentifierInfo *name, Location loc,
                         Expr *lower, Expr *upper,
                         DeclRegion *parent)
    : TypeDecl(AST_IntegerDecl, name, loc, parent),
      DeclRegion(AST_IntegerDecl, parent),
      lowExpr(lower), highExpr(upper)
{
    // Clear the subclass bits.
    Ast::bits = 0;

    llvm::APInt lowVal;
    llvm::APInt highVal;

    assert(lower->isStaticDiscreteExpr());
    assert(upper->isStaticDiscreteExpr());

    lower->staticDiscreteValue(lowVal);
    upper->staticDiscreteValue(highVal);

    IntegerType *base = resource.createIntegerType(this, lowVal, highVal);
    CorrespondingType =
        resource.createIntegerSubtype(name, base, lowVal, highVal);
}

// Note that we could perform these initializations in the constructor, but it
// would cause difficulties when the language primitive types are first being
// declared.  For now this is a separate method which called separately.
void IntegerDecl::generateImplicitDeclarations(AstResource &resource)
{
    IntegerType *type = getBaseSubtype();
    Location loc = getLocation();

    addDecl(resource.createPrimitiveDecl(PO::EQ_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::NE_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::LT_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::GT_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::LE_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::GE_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::ADD_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::SUB_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::MUL_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::DIV_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::MOD_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::REM_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::POW_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::NEG_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::POS_op, loc, type, this));
}

//===----------------------------------------------------------------------===//
// IntegerSubtypeDecl

IntegerSubtypeDecl::IntegerSubtypeDecl(AstResource &resource,
                                       IdentifierInfo *name, Location loc,
                                       IntegerType *subtype, DeclRegion *parent)
    : SubtypeDecl(AST_IntegerSubtypeDecl, name, loc, parent)
{
    CorrespondingType = resource.createIntegerSubtype(name, subtype);
}

IntegerSubtypeDecl::IntegerSubtypeDecl(AstResource &resource,
                                       IdentifierInfo *name, Location loc,
                                       IntegerType *subtype,
                                       Expr *lower, Expr *upper,
                                       DeclRegion *parent)
    : SubtypeDecl(AST_IntegerSubtypeDecl, name, loc, parent)
{
    CorrespondingType =
        resource.createIntegerSubtype(name, subtype, lower, upper);
}

//===----------------------------------------------------------------------===//
// ArrayDecl
ArrayDecl::ArrayDecl(AstResource &resource,
                     IdentifierInfo *name, Location loc,
                     unsigned rank, DSTDefinition **indices,
                     Type *component, bool isConstrained, DeclRegion *parent)
    : TypeDecl(AST_ArrayDecl, name, loc, parent),
      DeclRegion(AST_ArrayDecl, parent),
      indices(indices, indices + rank)
{
    assert(rank != 0 && "Missing indices!");

    // Extract the type nodes of the DSTDefinitions.
    llvm::SmallVector<DiscreteType*, 8> indexTypes(rank);
    for (unsigned i = 0; i < rank; ++i)
        indexTypes[i] = indices[0]->getType();

    ArrayType *base = resource.createArrayType(
        this, rank, &indexTypes[0], component, isConstrained);

    // Create the first subtype.
    CorrespondingType = resource.createArraySubtype(name, base);
}

//===----------------------------------------------------------------------===//
// ArraySubtypeDecl

ArraySubtypeDecl::ArraySubtypeDecl(AstResource &resource,
                                   IdentifierInfo *name, Location loc,
                                   ArrayType *subtype, DeclRegion *parent)
    : SubtypeDecl(AST_ArraySubtypeDecl, name, loc, parent)
{
    CorrespondingType = resource.createArraySubtype(name, subtype);
}

ArraySubtypeDecl::ArraySubtypeDecl(AstResource &resource,
                                   IdentifierInfo *name, Location loc,
                                   ArrayType *subtype, DSTDefinition **indices,
                                   DeclRegion *parent)
    : SubtypeDecl(AST_ArraySubtypeDecl, name, loc, parent),
      indices(indices, indices + subtype->getRank())
{
    // Extract the type nodes of the DSTDefinitions.
    unsigned rank = subtype->getRank();
    llvm::SmallVector<DiscreteType*, 8> indexTypes(rank);
    for (unsigned i = 0; i < rank; ++i)
        indexTypes[i] = indices[0]->getType();

    CorrespondingType =
        resource.createArraySubtype(name, subtype, &indexTypes[0]);
}

//===----------------------------------------------------------------------===//
// RecordDecl
RecordDecl::RecordDecl(AstResource &resource, IdentifierInfo *name,
                       Location loc, DeclRegion *parent)
    : TypeDecl(AST_RecordDecl, name, loc, parent),
      DeclRegion(AST_RecordDecl, parent), componentCount(0)
{
    RecordType *base = resource.createRecordType(this);
    CorrespondingType = resource.createRecordSubtype(name, base);
}

ComponentDecl *RecordDecl::addComponent(IdentifierInfo *name, Location loc,
                                        Type *type)
{
    ComponentDecl *component;
    component = new ComponentDecl(name, loc, type, componentCount, this);
    componentCount++;
    addDecl(component);
    return component;
}

ComponentDecl *RecordDecl::getComponent(unsigned i)
{
    // FIXME: Compensate for the presence of an equality operator when present.
    return cast<ComponentDecl>(getDecl(i));
}

ComponentDecl *RecordDecl::getComponent(IdentifierInfo *name)
{
    PredRange range = findDecls(name);
    if (range.first == range.second)
        return 0;
    return cast<ComponentDecl>(*range.first);
}

//===----------------------------------------------------------------------===//
// AccessDecl
AccessDecl::AccessDecl(AstResource &resource, IdentifierInfo *name, Location loc,
                       Type *targetType, DeclRegion *parent)
    : TypeDecl(AST_AccessDecl, name, loc, parent),
      DeclRegion(AST_AccessDecl, parent)
{
    AccessType *base = resource.createAccessType(this, targetType);
    CorrespondingType = resource.createAccessSubtype(name, base);
}

void AccessDecl::generateImplicitDeclarations(AstResource &resource)
{
    // FIXME: We will eventually need to specify the operand type specifically
    // as an unconstrained access type.
    AccessType *type = getType();
    Location loc = getLocation();

    addDecl(resource.createPrimitiveDecl(PO::EQ_op, loc, type, this));
    addDecl(resource.createPrimitiveDecl(PO::NE_op, loc, type, this));
}
