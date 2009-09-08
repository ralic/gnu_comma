//===-- typecheck/TypeCheck.cpp ------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008-2009 Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "DeclProducer.h"
#include "Scope.h"
#include "comma/typecheck/TypeCheck.h"
#include "comma/ast/Expr.h"
#include "comma/ast/Decl.h"
#include "comma/ast/Qualifier.h"
#include "comma/ast/Stmt.h"

#include "llvm/ADT/DenseMap.h"

#include <cstring>

using namespace comma;
using llvm::dyn_cast;
using llvm::dyn_cast_or_null;
using llvm::cast;
using llvm::isa;

TypeCheck::TypeCheck(Diagnostic      &diag,
                     AstResource     &resource,
                     CompilationUnit *cunit)
    : diagnostic(diag),
      resource(resource),
      compUnit(cunit),
      scope(new Scope),
      errorCount(0),
      declProducer(new DeclProducer(resource))
{
    populateInitialEnvironment();
}

TypeCheck::~TypeCheck()
{
    delete scope;
    delete declProducer;
}

// Called when then type checker is constructed.  Populates the top level scope
// with an initial environment.
void TypeCheck::populateInitialEnvironment()
{
    EnumerationDecl *theBoolDecl = declProducer->getBoolDecl();
    scope->addDirectDecl(theBoolDecl);
    importDeclRegion(theBoolDecl);

    IntegerDecl *theIntegerDecl = declProducer->getIntegerDecl();
    scope->addDirectDecl(theIntegerDecl);
    importDeclRegion(theIntegerDecl);
}

void TypeCheck::deleteNode(Node &node)
{
    Ast *ast = lift_node<Ast>(node);
    if (ast && ast->isDeletable()) delete ast;
    node.release();
}

Sigoid *TypeCheck::getCurrentSigoid() const
{
    return dyn_cast<Sigoid>(getCurrentModel());
}

SignatureDecl *TypeCheck::getCurrentSignature() const
{
    return dyn_cast<SignatureDecl>(getCurrentModel());
}

VarietyDecl *TypeCheck::getCurrentVariety() const
{
    return dyn_cast<VarietyDecl>(getCurrentModel());
}

Domoid *TypeCheck::getCurrentDomoid() const
{
    return dyn_cast<Domoid>(getCurrentModel());
}

DomainDecl *TypeCheck::getCurrentDomain() const
{
    return dyn_cast<DomainDecl>(getCurrentModel());
}

FunctorDecl *TypeCheck::getCurrentFunctor() const
{
    return dyn_cast<FunctorDecl>(getCurrentModel());
}

SubroutineDecl *TypeCheck::getCurrentSubroutine() const
{
    DeclRegion     *region = currentDeclarativeRegion();
    SubroutineDecl *routine;

    while (region) {
        if ((routine = dyn_cast<SubroutineDecl>(region)))
            return routine;
        region = region->getParent();
    }
    return 0;
}

ProcedureDecl *TypeCheck::getCurrentProcedure() const
{
    return dyn_cast_or_null<ProcedureDecl>(getCurrentSubroutine());
}

FunctionDecl *TypeCheck::getCurrentFunction() const
{
    return dyn_cast_or_null<FunctionDecl>(getCurrentSubroutine());
}

PercentDecl *TypeCheck::getCurrentPercent() const
{
    if (ModelDecl *model = getCurrentModel())
        return model->getPercent();
    return 0;
}

DomainType *TypeCheck::getCurrentPercentType() const
{
    if (ModelDecl *model = getCurrentModel())
        return model->getPercentType();
    return 0;
}

void TypeCheck::beginCapsule()
{
    assert(scope->getLevel() == 0 && "Cannot typecheck nested capsules!");

    // Push a scope for the upcoming capsule and reset our per-capsule state.
    scope->push(MODEL_SCOPE);
    GenericFormalDecls.clear();
    declarativeRegion = 0;
    currentModel = 0;
}

void TypeCheck::endCapsule()
{
    assert(scope->getKind() == MODEL_SCOPE);
    scope->pop();

    ModelDecl *result = getCurrentModel();
    if (Decl *conflict = scope->addDirectDecl(result)) {
        // NOTE: The result model could be freed here.
        report(result->getLocation(), diag::CONFLICTING_DECLARATION)
            << result->getIdInfo() << getSourceLoc(conflict->getLocation());
    }
    else
        compUnit->addDeclaration(result);
}

void TypeCheck::beginGenericFormals()
{
    assert(GenericFormalDecls.empty() && "Formal decls already present!");
}

void TypeCheck::endGenericFormals() { }

void TypeCheck::beginFormalDomainDecl(IdentifierInfo *name, Location loc)
{
    // Create an abstract domain declaration to represent this formal.
    AbstractDomainDecl *formal = new AbstractDomainDecl(name, loc);

    // Set the current declarative region to be that of the formal.
    declarativeRegion = formal;

    // Push a scope for the declaration and add the formal itself.
    scope->push();
    scope->addDirectDeclNoConflicts(formal);
    GenericFormalDecls.push_back(formal);
}

void TypeCheck::endFormalDomainDecl()
{
    // Pop the scope for the formal decl.  We sould be back in the scope of the
    // capsule being defined.
    scope->pop();
    assert(scope->getKind() == MODEL_SCOPE);

    // Add the most recent generic declaration into the scope for the model,
    // checking for conflicts.
    if (Decl *conflict = scope->addDirectDecl(GenericFormalDecls.back())) {
        // The only conflict possible is with a pervious formal parameter.
        AbstractDomainDecl *dom = cast<AbstractDomainDecl>(conflict);
        report(dom->getLocation(), diag::DUPLICATE_FORMAL_PARAM)
            << dom->getIdInfo();
        GenericFormalDecls.pop_back();
    }
}

void TypeCheck::beginDomainDecl(IdentifierInfo *name, Location loc)
{
    // If we have processed generic arguments, construct a functor, else a
    // domain.
    unsigned arity = GenericFormalDecls.size();

    if (arity == 0)
        currentModel = new DomainDecl(resource, name, loc);
    else
        currentModel = new FunctorDecl(resource, name, loc,
                                       &GenericFormalDecls[0], arity);
    initializeForModelDeclaration();
}

void TypeCheck::beginSignatureDecl(IdentifierInfo *name, Location loc)
{
    // If we have processed generic arguments, construct a variety, else a
    // signature.
    unsigned arity = GenericFormalDecls.size();

    if (arity == 0)
        currentModel = new SignatureDecl(resource, name, loc);
    else
        currentModel = new VarietyDecl(resource, name, loc,
                                       &GenericFormalDecls[0], arity);
    initializeForModelDeclaration();
}

void TypeCheck::initializeForModelDeclaration()
{
    assert(scope->getKind() == MODEL_SCOPE);

    // Set the current declarative region to be the percent node of the current
    // model.
    declarativeRegion = currentModel->getPercent();

    // For each generic formal, set its declarative region to be that the new
    // models percent node.
    unsigned arity = currentModel->getArity();
    for (unsigned i = 0; i < arity; ++i) {
        AbstractDomainDecl *formal = currentModel->getFormalDecl(i);
        formal->setDeclRegion(declarativeRegion);
    }

    // Bring the model itself into the current scope.  This should never result
    // in a conflict.
    scope->addDirectDeclNoConflicts(currentModel);
}

void TypeCheck::acceptSupersignature(Node typeNode, Location loc)
{
    SigInstanceDecl *superSig = lift_node<SigInstanceDecl>(typeNode);
    Sigoid *sigoid = superSig->getSigoid();

    // Check that the node denotes a signature.
    if (!superSig) {
        report(loc, diag::NOT_A_SIGNATURE);
        return;
    }
    typeNode.release();

    // We are either processing a model or a generic formal domain.  Add the
    // signature.
    if (ModelDecl *model = getCurrentModel()) {
        model->addDirectSignature(superSig);
    }
    else {
        // FIXME: Use a cleaner interface when available.
        AbstractDomainDecl *decl = cast<AbstractDomainDecl>(declarativeRegion);
        decl->addSuperSignature(superSig);
    }

    // Bring all types defined by this super signature into scope (so that they
    // can participate in upcomming type expressions) and add them to the
    // current model.
    aquireSignatureTypeDeclarations(declarativeRegion, sigoid);

    // Bring all implicit declarations defined by the super signature types into
    // scope.  This allows us to detect conflicting declarations as we process
    // the body.  All other subroutine declarations are processed after in
    // ensureNecessaryRedeclarations.
    aquireSignatureImplicitDeclarations(sigoid);
}

Node TypeCheck::acceptPercent(Location loc)
{
    // We are either processing a model or a generic formal domain.
    //
    // When processing a model, return the associated percent decl.  When
    // processing a generic formal domain, return the AbstractDomainDecl.
    if (ModelDecl *model = getCurrentModel()) {
        return getNode(model->getPercent());
    }
    else {
        // FIXME: Use a cleaner interface when available.
        AbstractDomainDecl *decl = cast<AbstractDomainDecl>(declarativeRegion);
        return getNode(decl);
    }
}

// Returns true if the given decl is equivalent to % in the context of the
// current domain.
//
// FIXME: This does not work when processing a formal domain.
bool TypeCheck::denotesDomainPercent(const Decl *decl)
{
    if (checkingDomain()) {
        DomainDecl *domain = getCurrentDomain();
        const DomainDecl *candidate = dyn_cast<DomainDecl>(decl);
        if (candidate and domain)
            return domain == candidate;
    }
    return false;
}

// Returns true if we are currently checking a functor, and if the given functor
// declaration together with the provided arguments would denote an instance
// which is equivalent to % in the current context.  For example, given:
//
//   domain F (X : T) with
//      procedure Foo (A : F(X));
//      ...
//
// Then "F(X)" is equivalent to %.  More generally, a functor F applied to its
// formal arguments in the body of F is equivalent to %.
//
// This function assumes that the number and types of the supplied arguments are
// compatible with the given functor.
bool TypeCheck::denotesFunctorPercent(const FunctorDecl *functor,
                                      DomainTypeDecl **args, unsigned numArgs)
{
    assert(functor->getArity() == numArgs);

    if (checkingFunctor()) {
        FunctorDecl *currentFunctor = getCurrentFunctor();
        if (currentFunctor != functor)
            return false;
        for (unsigned i = 0; i < numArgs; ++i) {
            DomainType *formal = currentFunctor->getFormalType(i);
            if (formal != args[i]->getType())
                return false;
        }
        return true;
    }
    return false;
}

bool TypeCheck::ensureNonRecursiveInstance(
    FunctorDecl *decl, DomainTypeDecl **args, unsigned numArgs, Location loc)
{
    if (!checkingFunctor() || (decl != getCurrentFunctor()))
        return true;
    for (unsigned i = 0; i < numArgs; ++i) {
        // FIXME: DomainTypeDecls should provide the involvesPercent method.
        DomainType *argTy = args[i]->getType();
        if (argTy->involvesPercent()) {
            report(loc, diag::SELF_RECURSIVE_INSTANCE);
            return false;
        }
    }
    return true;
}

/// Resolves the argument type of a Functor or Variety given previous actual
/// arguments.
///
/// For a dependent argument list of the form <tt>(X : T, Y : U(X))</tt>, this
/// function resolves the type of \c U(X) given an actual parameter for \c X.
/// It is assumed that the actual arguments provided are compatable with the
/// given model.
SigInstanceDecl *
TypeCheck::resolveFormalSignature(ModelDecl *parameterizedModel,
                                  Type **arguments, unsigned numArguments)
{
    assert(parameterizedModel->isParameterized());
    assert(numArguments < parameterizedModel->getArity());

    AstRewriter rewriter(resource);

    // For each actual argument, establish a map from the formal parameter to
    // the actual.
    for (unsigned i = 0; i < numArguments; ++i) {
        Type *formal = parameterizedModel->getFormalType(i);
        Type *actual = arguments[i];
        rewriter.addRewrite(formal, actual);
    }

    SigInstanceDecl *target = parameterizedModel->getFormalSignature(numArguments);
    return rewriter.rewrite(target);
}

Decl *TypeCheck::resolveTypeOrModelDecl(IdentifierInfo *name,
                                        Location loc, DeclRegion *region)
{
    Decl *result = 0;

    if (region) {
        DeclRegion::PredRange range = region->findDecls(name);
        // Search the region for a type of the given name.  Type names do not
        // overload so if the type exists, it is unique, and the first match is
        // accepted.
        for (DeclRegion::PredIter iter = range.first;
             iter != range.second; ++iter) {
            Decl *candidate = *iter;
            if ((result = dyn_cast<ModelDecl>(candidate)) or
                (result = dyn_cast<TypeDecl>(candidate)))
                break;
        }
    }
    else {
        Scope::Resolver &resolver = scope->getResolver();
        if (resolver.resolve(name)) {
            if (resolver.hasDirectType())
                result = resolver.getDirectType();
            else if (resolver.hasDirectCapsule())
                result = resolver.getDirectCapsule();
            else if (resolver.hasIndirectTypes()) {
                // For the lookup not to be ambiguous, there must only be one
                // indirect type name accessible.
                if (resolver.numIndirectTypes() > 1 ||
                    resolver.hasIndirectOverloads() ||
                    resolver.hasIndirectValues()) {
                    report(loc, diag::NAME_REQUIRES_QUAL) << name;
                    return 0;
                }
                result = resolver.getIndirectType(0);
            }
        }
    }
    if (result == 0)
        report(loc, diag::TYPE_NOT_VISIBLE) << name;
    return result;
}

Node TypeCheck::acceptTypeName(IdentifierInfo *id, Location loc, Node qualNode)
{
    Decl *decl;

    if (qualNode.isNull()) {
        // FIXME:  Use a Scope::Resolver here.
        decl = resolveTypeOrModelDecl(id, loc);
    }
    else {
        Qualifier *qualifier = cast_node<Qualifier>(qualNode);
        DeclRegion *region = resolveVisibleQualifiedRegion(qualifier);
        decl = resolveTypeOrModelDecl(id, loc, region);
    }

    if (decl == 0)
        return getInvalidNode();

    switch (decl->getKind()) {

    default:
        assert(false && "Cannot handle type declaration.");
        return getInvalidNode();

    case Ast::AST_DomainDecl: {
        if (denotesDomainPercent(decl)) {
            report(loc, diag::PERCENT_EQUIVALENT);
            return getNode(getCurrentPercent());
        }
        DomainDecl *domDecl = cast<DomainDecl>(decl);
        return getNode(domDecl->getInstance());
    }

    case Ast::AST_SignatureDecl: {
        SignatureDecl *sigDecl = cast<SignatureDecl>(decl);
        return getNode(sigDecl->getInstance());
    }

    case Ast::AST_AbstractDomainDecl:
    case Ast::AST_CarrierDecl:
    case Ast::AST_EnumerationDecl:
    case Ast::AST_IntegerDecl:
        return getNode(decl);

    case Ast::AST_FunctorDecl:
    case Ast::AST_VarietyDecl:
        report(loc, diag::WRONG_NUM_ARGS_FOR_TYPE) << id;
        return getInvalidNode();
    }
}

Node TypeCheck::acceptTypeApplication(IdentifierInfo  *connective,
                                      NodeVector      &argumentNodes,
                                      Location        *argumentLocs,
                                      IdentifierInfo **keywords,
                                      Location        *keywordLocs,
                                      unsigned         numKeywords,
                                      Location         loc)
{
    Scope::Resolver &resolver = scope->getResolver();
    if (!resolver.resolve(connective) || !resolver.hasDirectCapsule()) {
        report(loc, diag::TYPE_NOT_VISIBLE) << connective;
        return getInvalidNode();
    }

    ModelDecl *model = resolver.getDirectCapsule();
    unsigned numArgs = argumentNodes.size();
    assert(numKeywords <= numArgs && "More keywords than arguments!");

    if (!model->isParameterized() || model->getArity() != numArgs) {
        report(loc, diag::WRONG_NUM_ARGS_FOR_TYPE) << connective;
        return getInvalidNode();
    }

    unsigned numPositional = numArgs - numKeywords;
    llvm::SmallVector<DomainTypeDecl*, 4> arguments(numArgs);

    // First, populate the argument vector with any positional parameters.
    //
    // FIXME: Currently only DomainTypeDecls and SigInstanceDecls propagate as
    // arguments to a type application.  We should factor this out into a
    // seperate pass over the argument vector.
    for (unsigned i = 0; i < numPositional; ++i) {
        DomainTypeDecl *arg = lift_node<DomainTypeDecl>(argumentNodes[i]);
        if (!arg) {
            SigInstanceDecl *sig =
                cast_node<SigInstanceDecl>(argumentNodes[i]);
            Location loc = argumentLocs[i];
            report(loc, diag::SIGNATURE_AS_TYPE_PARAM) << sig->getIdInfo();
            return getInvalidNode();
        }
        arguments[i] = arg;
    }

    // Process any keywords provided.
    for (unsigned i = 0; i < numKeywords; ++i) {
        IdentifierInfo *keyword = keywords[i];
        Location keywordLoc = keywordLocs[i];
        int keywordIdx = model->getKeywordIndex(keyword);

        // Ensure the given keyword exists.
        if (keywordIdx < 0) {
            report(keywordLoc, diag::TYPE_HAS_NO_SUCH_KEYWORD)
                << keyword << connective;
            return getInvalidNode();
        }

        // The corresponding index of the keyword must be greater than the
        // number of supplied positional parameters (otherwise it would
        // `overlap' a positional parameter).
        if ((unsigned)keywordIdx < numPositional) {
            report(keywordLoc, diag::PARAM_PROVIDED_POSITIONALLY) << keyword;
            return getInvalidNode();
        }

        // Ensure that this keyword is not a duplicate of any preceeding
        // keyword.
        for (unsigned j = 0; j < i; ++j) {
            if (keywords[j] == keyword) {
                report(keywordLoc, diag::DUPLICATE_KEYWORD) << keyword;
                return getInvalidNode();
            }
        }

        // Lift the argument node and add it to the set of arguments in its
        // proper position.
        unsigned argIdx = i + numPositional;
        DomainTypeDecl *argument =
            lift_node<DomainTypeDecl>(argumentNodes[argIdx]);

        // FIXME: Currently only DomainTypeDecls and SigInstanceDecls propagate
        // as arguments to a type application.  We should factor this out into a
        // seperate pass over the argument vector.
        if (!argument) {
            SigInstanceDecl *sig =
                cast_node<SigInstanceDecl>(argumentNodes[argIdx]);
            Location loc = argumentLocs[argIdx];
            report(loc, diag::SIGNATURE_AS_TYPE_PARAM) << sig->getIdInfo();
            return getInvalidNode();
        }

        argumentNodes[i + numPositional].release();
        arguments[keywordIdx] = argument;
    }

    // Check each argument type.
    //
    // FIXME:  Factor this out.
    for (unsigned i = 0; i < numArgs; ++i) {
        DomainType *argTy = arguments[i]->getType();
        Location argLoc = argumentLocs[i];
        AbstractDomainDecl *target = model->getFormalDecl(i);

        // Establish a rewriter mapping all previous formals to the given
        // actuals, and from the target to the argument (abstract domain decls
        // have % rewritten to denote themselves, which in this case we want to
        // map to the type of the actual).
        AstRewriter rewrites(resource);
        rewrites[target->getType()] = arguments[i]->getType();
        for (unsigned j = 0; j < i; ++j)
            rewrites[model->getFormalType(j)] = arguments[j]->getType();

        if (!checkSignatureProfile(rewrites, argTy, target, argLoc))
            return getInvalidNode();
    }

    // Obtain a memoized type node for this particular argument set.
    Node node = getInvalidNode();
    if (VarietyDecl *variety = dyn_cast<VarietyDecl>(model))
        node = getNode(variety->getInstance(arguments.data(), numArgs));
    else {
        FunctorDecl *functor = cast<FunctorDecl>(model);

        if (!ensureNonRecursiveInstance(
                functor, arguments.data(), numArgs, loc))
            return getInvalidNode();

        if (denotesFunctorPercent(functor, arguments.data(), numArgs)) {
            // Cannonicalize type applications which are equivalent to `%'.
            report(loc, diag::PERCENT_EQUIVALENT);
            node = getNode(getCurrentPercent());
        }
        else {
            DomainInstanceDecl *instance =
                functor->getInstance(arguments.data(), numArgs);
            node = getNode(instance);
        }
    }
    argumentNodes.release();
    return node;
}

void TypeCheck::beginSignatureProfile()
{
    // Nothing to do.  The declarative region and scope of the current model or
    // formal domain is the destination of all declarations in a with
    // expression.
}

void TypeCheck::endSignatureProfile()
{
    DomainTypeDecl *domain;

    // Ensure that all ambiguous declarations are redeclared.  For now, the only
    // ambiguity that can arise is wrt conflicting argument keyword sets.
    if (ModelDecl *model = getCurrentModel())
        domain = model->getPercent();
    else
        domain = cast<AbstractDomainDecl>(declarativeRegion);

    ensureNecessaryRedeclarations(domain);
}

// Creates a procedure or function decl depending on the kind of the
// supplied type.
SubroutineDecl *
TypeCheck::makeSubroutineDecl(SubroutineDecl *SRDecl,
                              const AstRewriter &rewrites, DeclRegion *region)
{
    IdentifierInfo *name = SRDecl->getIdInfo();
    SubroutineType *SRType = rewrites.rewrite(SRDecl->getType());
    unsigned arity = SRDecl->getArity();

    llvm::SmallVector<IdentifierInfo*, 8> keys;
    for (unsigned i = 0; i < arity; ++i)
        keys.push_back(SRDecl->getParamKeyword(i));

    SubroutineDecl *result;
    if (FunctionType *ftype = dyn_cast<FunctionType>(SRType))
        result =  new FunctionDecl(name, 0, keys.data(), ftype, region);
    else {
        ProcedureType *ptype = cast<ProcedureType>(SRType);
        result = new ProcedureDecl(name, 0, keys.data(), ptype, region);
    }

    // Ensure the result declaration has the same parameter modes as the
    // original;
    for (unsigned i = 0; i < arity; ++i) {
        ParamValueDecl *param = result->getParam(i);
        param->setParameterMode(SRDecl->getExplicitParamMode(i));
    }

    return result;
}

TypeDecl *TypeCheck::ensureTypeDecl(Decl *decl, Location loc, bool report)
{
    if (TypeDecl *tyDecl = dyn_cast<TypeDecl>(decl))
        return tyDecl;
    if (report)
        this->report(loc, diag::TYPE_CANNOT_DENOTE_VALUE);
    return 0;
}

TypeDecl *TypeCheck::ensureTypeDecl(Node node, Location loc, bool report)
{
    Decl *decl = cast_node<Decl>(node);
    return ensureTypeDecl(decl, loc, report);
}

/// Returns true if \p expr is a static integer expression.  If so, initializes
/// \p result to a signed value which can accommodate the given static
/// expression.
bool TypeCheck::ensureStaticIntegerExpr(Expr *expr, llvm::APInt &result)
{
    // FIXME: IntegerLiterals are not the only kind of static integer
    // expression!
    if (IntegerLiteral *ILit = dyn_cast<IntegerLiteral>(expr)) {
        result = ILit->getValue();
        return true;
    }
    else {
        report(expr->getLocation(), diag::NON_STATIC_EXPRESSION);
        return false;
    }
}

void TypeCheck::ensureNecessaryRedeclarations(DomainTypeDecl *domain)
{
    assert(isa<PercentDecl>(domain) || isa<AbstractDomainDecl>(domain));

    // We scan the set of declarations for each direct signature of the given
    // domain.  When a declaration is found which has not already been declared
    // and is not overriden, we add it on good faith that all upcoming
    // declarations will not conflict.
    //
    // When a conflict occurs (that is, when two declarations exists with the
    // same name and type) we remember which immediate declarations in the
    // domain are invalid.  Once all the declarations are processed, we remove
    // those declarations found to be in conflict.
    typedef llvm::SmallPtrSet<Decl*, 4> BadDeclSet;
    BadDeclSet badDecls;

    // An "indirect decl", in this context, is a subroutine decl which is
    // inherited from a super signature.  We maintain a map from such indirect
    // decls to the declaration node supplied by the signature.  This allows us
    // to provide diagnostics which mention the location of conflicts.
    typedef std::pair<SubroutineDecl*, SubroutineDecl*> IndirectPair;
    typedef llvm::DenseMap<SubroutineDecl*, SubroutineDecl*> IndirectDeclMap;
    IndirectDeclMap indirectDecls;

    const SignatureSet &sigset = domain->getSignatureSet();
    SignatureSet::iterator superIter = sigset.beginDirect();
    SignatureSet::iterator endSuperIter = sigset.endDirect();
    for ( ; superIter != endSuperIter; ++superIter) {
        SigInstanceDecl *super = *superIter;
        Sigoid *sigdecl = super->getSigoid();
        PercentDecl *sigPercent = sigdecl->getPercent();
        AstRewriter rewrites(resource);

        rewrites[sigdecl->getPercentType()] = domain->getType();
        rewrites.installRewrites(super);

        DeclRegion::DeclIter iter    = sigPercent->beginDecls();
        DeclRegion::DeclIter endIter = sigPercent->endDecls();
        for ( ; iter != endIter; ++iter) {
            // Only subroutine declarations need to be redeclared.
            SubroutineDecl *srDecl = dyn_cast<SubroutineDecl>(*iter);
            if (!srDecl)
                continue;

            // If the routine is overriden, ignore it.
            if (domain->findOverridingDeclaration(srDecl))
                continue;

            // Rewrite the declaration to match the current models context.
            SubroutineDecl *rewriteDecl =
                makeSubroutineDecl(srDecl, rewrites, domain);
            Decl *conflict = scope->addDirectDecl(rewriteDecl);

            if (!conflict) {
                // Set the origin to point at the signature which originally
                // declared it.
                rewriteDecl->setOrigin(srDecl);
                domain->addDecl(rewriteDecl);
                indirectDecls.insert(IndirectPair(rewriteDecl, srDecl));
                continue;
            }

            // If the conflict is with respect to a TypeDecl, it must be a type
            // declared locally within the current model (since the set of types
            // provided by the super signatures have already been verified
            // consistent in aquireSignatureTypeDeclarations).  This is an
            // error.
            //
            // FIXME: assert that this is local to the current model.
            if (isa<TypeDecl>(conflict)) {
                SourceLocation sloc = getSourceLoc(srDecl->getLocation());
                report(conflict->getLocation(), diag::DECLARATION_CONFLICTS)
                    << srDecl->getIdInfo() << sloc;
                badDecls.insert(conflict);
            }

            // We currently do not support ValueDecls in models.  Therefore, the
            // conflict must denote a subroutine.
            SubroutineDecl *conflictRoutine = cast<SubroutineDecl>(conflict);

            // Ensure the parameter modes match, accounting for any overriding
            // declarations.
            if (!ensureMatchingParameterModes(conflictRoutine,
                                              srDecl, domain)) {
                badDecls.insert(conflictRoutine);
                continue;
            }

            if (conflictRoutine->keywordsMatch(rewriteDecl)) {
                // If the conflicting declaration does not have an origin
                // (meaning that is was explicitly declared by the model) map
                // its origin to that of the original subroutine provided by the
                // signature.
                if (!conflictRoutine->hasOrigin()) {
                    // FIXME: We should warn here that the "conflict"
                    // declaration is simply redundant.
                    conflictRoutine->setOrigin(srDecl);
                }
                continue;
            }

            // Otherwise, the keywords do not match.  If the conflicting
            // decl is a member of the indirect set, post a diagnostic.
            // Otherwise, the conflicting decl was declared by this model
            // and hense overrides the conflict.
            if (indirectDecls.count(conflictRoutine)) {
                Location modelLoc = domain->getLocation();
                SubroutineDecl *baseDecl =
                    indirectDecls.lookup(conflictRoutine);
                SourceLocation sloc1 =
                    getSourceLoc(baseDecl->getLocation());
                SourceLocation sloc2 =
                    getSourceLoc(srDecl->getLocation());
                report(modelLoc, diag::MISSING_REDECLARATION)
                    << srDecl->getIdInfo() << sloc1 << sloc2;
                badDecls.insert(conflictRoutine);
            }
        }
    }

    // Remove and clean up memory for each inherited node found to require a
    // redeclaration.
    for (BadDeclSet::iterator iter = badDecls.begin();
         iter != badDecls.end(); ++iter) {
        Decl *badDecl = *iter;
        domain->removeDecl(badDecl);
        delete badDecl;
    }
}

bool TypeCheck::ensureMatchingParameterModes(SubroutineDecl *X,
                                             SubroutineDecl *Y)
{
    unsigned arity = X->getArity();
    assert(arity == Y->getArity() && "Arity mismatch!");

    for (unsigned i = 0; i < arity; ++i) {
        if (X->getParamMode(i) != Y->getParamMode(i)) {
            ParamValueDecl *param = X->getParam(i);
            report(param->getLocation(),
                   diag::INCOMPATABLE_MODE_REDECLARATION)
                << getSourceLoc(Y->resolveOrigin()->getLocation());
            return false;
        }
    }
    return true;
}

bool TypeCheck::ensureMatchingParameterModes(
    SubroutineDecl *X, SubroutineDecl *Y, DeclRegion *region)
{
    unsigned arity = X->getArity();
    assert(arity == Y->getArity() && "Arity mismatch!");

    for (unsigned i = 0; i < arity; ++i) {
        if (X->getParamMode(i) != Y->getParamMode(i)) {
            // The parameter modes do not match.  Using the supplied declarative
            // region, check if any overriding declarations are in effect.
            if (region->findOverridingDeclaration(Y))
                return false;

            // None were found.
            if (X->isImmediate()) {
                // X is an immediate decl.  Use the location of the offending
                // parameter as a context.
                ParamValueDecl *param = X->getParam(i);
                report(param->getLocation(),
                       diag::INCOMPATABLE_MODE_REDECLARATION)
                    << getSourceLoc(Y->getLocation());
            }
            else {
                // Otherwise, resolve the declarative region to a PercentDecl or
                // AbstractDomainDecl and use the corresponding model as
                // context.  Form a cross reference diagnostic involving the
                // origin of X.
                Location contextLoc;
                if (PercentDecl *context = dyn_cast<PercentDecl>(region))
                    contextLoc = context->getDefinition()->getLocation();
                else {
                    AbstractDomainDecl *context =
                        cast<AbstractDomainDecl>(region);
                    contextLoc = context->getLocation();
                }
                report(contextLoc, diag::SUBROUTINE_OVERRIDE_REQUIRED)
                    << X->getIdInfo()
                    << getSourceLoc(X->getOrigin()->getLocation())
                    << getSourceLoc(Y->getLocation());
            }
            return false;
        }
    }
    return true;
}

void TypeCheck::aquireSignatureTypeDeclarations(DeclRegion *region,
                                                Sigoid *sigdecl)
{
    PercentDecl *sigPercent = sigdecl->getPercent();

    DeclRegion::DeclIter I = sigPercent->beginDecls();
    DeclRegion::DeclIter E = sigPercent->endDecls();
    for ( ; I != E; ++I) {
        if (TypeDecl *tyDecl = dyn_cast<TypeDecl>(*I)) {
            if (Decl *conflict = scope->addDirectDecl(tyDecl)) {
                // FIXME: We should not error if conflict is an equivalent type
                // decl, but we do not have support for such a concept yet.
                //
                // FIXME: We need a better diagnostic which reports context.
                SourceLocation sloc = getSourceLoc(conflict->getLocation());
                report(tyDecl->getLocation(), diag::DECLARATION_CONFLICTS)
                    << tyDecl->getIdInfo() << sloc;
            }
            else
                region->addDecl(tyDecl);
        }
    }
}

void TypeCheck::aquireSignatureImplicitDeclarations(Sigoid *sigdecl)
{
    PercentDecl *sigPercent = sigdecl->getPercent();

    DeclRegion::DeclIter I = sigPercent->beginDecls();
    DeclRegion::DeclIter E = sigPercent->endDecls();
    for ( ; I != E; ++I) {
        TypeDecl *tyDecl = dyn_cast<TypeDecl>(*I);

        if (!tyDecl)
            continue;

        // Currently, only enumeration and integer decls supply implicit
        // declarations.  Bringing these declarations into scope should never
        // result in a conflict since they should all involve unique types.
        DeclRegion *region;
        if (EnumerationDecl *eDecl = dyn_cast<EnumerationDecl>(tyDecl))
            region = eDecl;
        else if (IntegerDecl *iDecl = dyn_cast<IntegerDecl>(tyDecl))
            region = iDecl;

        DeclRegion::DeclIter II = region->beginDecls();
        DeclRegion::DeclIter EE = region->endDecls();
        for ( ; II != EE; ++II)
            scope->addDirectDeclNoConflicts(*II);
    }
}

bool TypeCheck::acceptObjectDeclaration(Location loc, IdentifierInfo *name,
                                        Node declNode, Node initializerNode)
{
    Expr *init = 0;
    TypeDecl *tyDecl = ensureTypeDecl(declNode, loc);

    if (!tyDecl) return false;

    if (!initializerNode.isNull()) {
        init = cast_node<Expr>(initializerNode);
        if (!checkExprInContext(init, tyDecl->getType()))
            return false;
    }
    ObjectDecl *decl = new ObjectDecl(name, tyDecl->getType(), loc, init);

    declNode.release();
    initializerNode.release();

    if (Decl *conflict = scope->addDirectDecl(decl)) {
        SourceLocation sloc = getSourceLoc(conflict->getLocation());
        report(loc, diag::DECLARATION_CONFLICTS) << name << sloc;
        return false;
    }
    currentDeclarativeRegion()->addDecl(decl);
    return true;
}

bool TypeCheck::acceptImportDeclaration(Node importedNode, Location loc)
{
    Decl *decl = cast_node<Decl>(importedNode);
    DomainType *domain;

    if (CarrierDecl *carrier = dyn_cast<CarrierDecl>(decl))
        domain = dyn_cast<DomainType>(carrier->getRepresentationType());
    else
        domain = dyn_cast<DomainTypeDecl>(decl)->getType();

    if (!domain) {
        report(loc, diag::IMPORT_FROM_NON_DOMAIN);
        return false;
    }

    importedNode.release();
    scope->addImport(domain);

    // FIXME:  We need to stitch this import declaration into the current
    // context.
    new ImportDecl(domain, loc);
    return true;
}

void TypeCheck::beginAddExpression()
{
    Domoid *domoid = getCurrentDomoid();
    assert(domoid && "Processing `add' expression outside domain context!");

    // Switch to the declarative region which this domains AddDecl provides.
    declarativeRegion = domoid->getImplementation();
    assert(declarativeRegion && "Domain missing Add declaration node!");

    // Enter a new scope for the add expression.
    scope->push();
}

void TypeCheck::endAddExpression()
{
    ensureExportConstraints(getCurrentDomoid()->getImplementation());

    // Leave the scope corresponding to the add expression and switch back to
    // the declarative region of the defining domains percent node.
    declarativeRegion = declarativeRegion->getParent();
    assert(declarativeRegion == getCurrentPercent()->asDeclRegion());
    scope->pop();
}

void TypeCheck::acceptCarrier(IdentifierInfo *name, Node declNode, Location loc)
{
    // We should always be in an add declaration.
    AddDecl *add = cast<AddDecl>(declarativeRegion);

    if (add->hasCarrier()) {
        report(loc, diag::MULTIPLE_CARRIER_DECLARATIONS);
        return;
    }

    if (TypeDecl *tyDecl = ensureTypeDecl(declNode, loc)) {
        declNode.release();
        Type *carrierTy = tyDecl->getType();
        CarrierDecl *carrier = new CarrierDecl(name, carrierTy, loc);
        if (Decl *conflict = scope->addDirectDecl(carrier)) {
            report(loc, diag::CONFLICTING_DECLARATION)
                << name << getSourceLoc(conflict->getLocation());
            return;
        }
        add->setCarrier(carrier);
    }
}

// There is nothing for us to do at the start of a subroutine declaration.
// Creation of the declaration itself is deferred until
// acceptSubroutineDeclaration is called.
void TypeCheck::beginSubroutineDeclaration(Descriptor &desc)
{
    assert((desc.isFunctionDescriptor() || desc.isProcedureDescriptor()) &&
           "Beginning a subroutine which is neither a function or procedure?");
}

Node TypeCheck::acceptSubroutineParameter(IdentifierInfo *formal, Location loc,
                                          Node declNode, PM::ParameterMode mode)
{
    // FIXME: The location provided here is the location of the formal, not the
    // location of the type.  The decl node here should be a ModelRef or similar
    // which encapsulates the needed location information.
    TypeDecl *tyDecl = ensureTypeDecl(declNode, loc);

    if (!tyDecl) return getInvalidNode();

    declNode.release();
    Type *paramTy = tyDecl->getType();
    ParamValueDecl *paramDecl = new ParamValueDecl(formal, paramTy, mode, loc);
    return getNode(paramDecl);
}

Node TypeCheck::acceptSubroutineDeclaration(Descriptor &desc,
                                            bool definitionFollows)
{
    assert((desc.isFunctionDescriptor() || desc.isProcedureDescriptor()) &&
           "Descriptor does not denote a subroutine!");

    // If we uncover a problem with the subroutines parameters, the following
    // flag is set to false.  Note that we do not create a declaration for the
    // subroutine unless it checks out 100%.
    //
    // Start by ensuring all parameters are distinct.
    bool paramsOK = checkDescriptorDuplicateParams(desc);
    IdentifierInfo *name = desc.getIdInfo();
    Location location = desc.getLocation();

    // Every parameter of this descriptor should be a ParamValueDecl.  As we
    // validate the type of each node, test that no duplicate formal parameters
    // are accumulated.  Any duplicates found are discarded.
    typedef llvm::SmallVector<ParamValueDecl*, 6> paramVec;
    paramVec parameters;
    convertDescriptorParams<ParamValueDecl>(desc, parameters);

    // If this is a function descriptor, ensure that every parameter is of mode
    // "in".  Also, ensure that if this function names a binary operator it has
    // arity 2.
    if (desc.isFunctionDescriptor()) {
        for (paramVec::iterator I = parameters.begin();
             I != parameters.end(); ++I)
            paramsOK = checkFunctionParameter(*I);
        if (parameters.size() != 2 and namesBinaryFunction(desc.getIdInfo())) {
            report(location, diag::BINARY_FUNCTION_ARITY_MISMATCH) << name;
            paramsOK = false;
        }
    }

    // If the parameters did not check, stop.
    if (!paramsOK)
        return getInvalidNode();

    SubroutineDecl *routineDecl = 0;
    DeclRegion *region = currentDeclarativeRegion();
    if (desc.isFunctionDescriptor()) {
        if (TypeDecl *returnDecl = ensureTypeDecl(desc.getReturnType(), 0)) {
            Type *returnType = returnDecl->getType();
            routineDecl = new FunctionDecl(resource, name, location,
                                           parameters.data(), parameters.size(),
                                           returnType, region);
        }
    }
    else {
        routineDecl = new ProcedureDecl(resource, name, location,
                                        parameters.data(), parameters.size(),
                                        region);
    }
    if (!routineDecl) return getInvalidNode();

    // Ensure this new declaration does not conflict with any other currently in
    // scope.
    if (Decl *conflict = scope->addDirectDecl(routineDecl)) {
        report(location, diag::CONFLICTING_DECLARATION)
            << name << getSourceLoc(conflict->getLocation());
        return getInvalidNode();
    }

    // Add the subroutine to the current declarative region and mark it as
    // immediate (e.g. not inherited).
    region->addDecl(routineDecl);
    routineDecl->setImmediate();

    // Since the declaration has been added permanently to the environment,
    // ensure the returned Node does not reclaim the decl.
    Node routine = getNode(routineDecl);
    routine.release();
    desc.release();
    return routine;
}

void TypeCheck::acceptOverrideTarget(Node qualNode,
                                     IdentifierInfo *name, Location loc,
                                     Node declarationNode)
{
    // The grammer does not enforce that the name to override must be
    // qualified.
    if (qualNode.isNull()) {
        report(loc, diag::EXPECTING_SIGNATURE_QUALIFIER) << name;
        return;
    }

    // Ensure that the qualifier resolves to a signature.
    Qualifier *qual = cast_node<Qualifier>(qualNode);
    SigInstanceDecl *sig = qual->resolve<SigInstanceDecl>();
    Location sigLoc = qual->getBaseLocation();

    if (!sig) {
        Decl *base = qual->getBaseDecl();
        report(sigLoc, diag::NOT_A_SUPERSIGNATURE) << base->getIdInfo();
        return;
    }
    PercentDecl *sigPercent = sig->getSigoid()->getPercent();

    // Resolve the current context and ensure the given instance denotes a super
    // signature.
    DomainTypeDecl *context;
    if (PercentDecl *percent = getCurrentPercent())
        context = percent;
    else {
        // FIXME: Use a cleaner interface when available.
        context = cast<AbstractDomainDecl>(declarativeRegion);
    }

    if (!context->getSignatureSet().contains(sig)) {
        report(sigLoc, diag::NOT_A_SUPERSIGNATURE) << sig->getIdInfo();
        return;
    }

    // Depending on the kind of declaration we were supplied with, collect all
    // subroutine declarations with the given name.
    SubroutineDecl *overridingDecl =
        cast_node<SubroutineDecl>(declarationNode);
    typedef llvm::SmallVector<SubroutineDecl*, 8> TargetVector;
    TargetVector targets;
    if (isa<FunctionDecl>(overridingDecl))
        sigPercent->collectFunctionDecls(name, targets);
    else {
        assert(isa<ProcedureDecl>(overridingDecl));
        sigPercent->collectProcedureDecls(name, targets);
    }

    // If we did not resolve a single name, diagnose and return.
    if (targets.empty()) {
        report(loc, diag::NOT_A_COMPONENT_OF) << name << sig->getIdInfo();
        return;
    }

    // Create a set of rewrite rules compatable with the given signature
    // instance.
    AstRewriter rewrites(resource);
    rewrites.installRewrites(sig);
    rewrites[sigPercent->getType()] = context->getType();

    // Iterate over the set of targets.  Compare the rewritten version of each
    // target type with the type of our overriding decl.  If we find a match,
    // the construct is valid.
    SubroutineType *overridingType = overridingDecl->getType();
    for (TargetVector::iterator I = targets.begin(); I != targets.end(); ++I) {
        SubroutineDecl *targetDecl = *I;
        SubroutineType *targetType = targetDecl->getType();
        SubroutineType *rewriteType = rewrites.rewrite(targetType);
        if ((overridingType == rewriteType) &&
            overridingDecl->paramModesMatch(targetDecl)) {
            overridingDecl->setOverriddenDecl(targetDecl);
            return;
        }
    }

    // Otherwise, no match was found.
    report(loc, diag::INCOMPATABLE_OVERRIDE)
        << overridingDecl->getIdInfo() << name;
    return;
}


void TypeCheck::beginSubroutineDefinition(Node declarationNode)
{
    SubroutineDecl *srDecl = cast_node<SubroutineDecl>(declarationNode);
    declarationNode.release();

    // Enter a scope for the subroutine definition.  Add the subroutine itself
    // as an element of the new scope and add the formal parameters.  This
    // should never result in conflicts.
    scope->push(FUNCTION_SCOPE);
    scope->addDirectDeclNoConflicts(srDecl);
    typedef SubroutineDecl::param_iterator param_iterator;
    for (param_iterator I = srDecl->begin_params();
         I != srDecl->end_params(); ++I)
        scope->addDirectDeclNoConflicts(*I);

    // Allocate a BlockStmt for the subroutines body and make this block the
    // current declarative region.
    assert(!srDecl->hasBody() && "Current subroutine already has a body!");
    BlockStmt *block = new BlockStmt(0, srDecl, srDecl->getIdInfo());
    srDecl->setBody(block);
    declarativeRegion = block;
}

void TypeCheck::acceptSubroutineStmt(Node stmt)
{
    SubroutineDecl *subroutine = getCurrentSubroutine();
    assert(subroutine && "No currnet subroutine!");

    stmt.release();
    BlockStmt *block = subroutine->getBody();
    block->addStmt(cast_node<Stmt>(stmt));
}

void TypeCheck::endSubroutineDefinition()
{
    assert(scope->getKind() == FUNCTION_SCOPE);

    // We established two levels of declarative regions in
    // beginSubroutineDefinition: one for the BlockStmt constituting the body
    // and another corresponding to the subroutine itself.  Pop them both.
    declarativeRegion = declarativeRegion->getParent()->getParent();
    scope->pop();
}

Node TypeCheck::acceptKeywordSelector(IdentifierInfo *key, Location loc,
                                      Node exprNode, bool forSubroutine)
{
    if (!forSubroutine) {
        assert(false && "cannot accept keyword selectors for types yet!");
        return getInvalidNode();
    }

    exprNode.release();
    Expr *expr = cast_node<Expr>(exprNode);
    return getNode(new KeywordSelector(key, loc, expr));
}

Node TypeCheck::beginEnumerationType(IdentifierInfo *name, Location loc)
{
    DeclRegion *region = currentDeclarativeRegion();
    EnumerationDecl *enumeration = new EnumerationDecl(name, loc, region);
    if (Decl *conflict = scope->addDirectDecl(enumeration)) {
        report(loc, diag::CONFLICTING_DECLARATION)
            << name << getSourceLoc(conflict->getLocation());
        return getInvalidNode();
    }
    return getNode(enumeration);
}

void TypeCheck::acceptEnumerationLiteral(Node enumerationNode,
                                         IdentifierInfo *name, Location loc)
{
    EnumerationDecl *enumeration = cast_node<EnumerationDecl>(enumerationNode);

    if (enumeration->containsDecl(name)) {
        report(loc, diag::MULTIPLE_ENUMERATION_LITERALS) << name;
        return;
    }
    new EnumLiteral(resource, name, loc, enumeration);
}

void TypeCheck::endEnumerationType(Node enumerationNode)
{
    DeclRegion *region = currentDeclarativeRegion();
    EnumerationDecl *enumeration = cast_node<EnumerationDecl>(enumerationNode);

    enumerationNode.release();
    declProducer->createImplicitDecls(enumeration);
    region->addDecl(enumeration);
    importDeclRegion(enumeration);
}

/// Called to process integer type definitions.
///
/// For example, given a definition of the form <tt>type T is range X..Y;</tt>,
/// this callback is invoked with \p name set to the identifier \c T, \p loc set
/// to the location of \p name, \p low set to the expression \c X, and \p high
/// set to the expression \c Y.
void TypeCheck::acceptIntegerTypedef(IdentifierInfo *name, Location loc,
                                     Node lowNode, Node highNode)
{
    DeclRegion *region = currentDeclarativeRegion();
    Expr *lowExpr = cast_node<Expr>(lowNode);
    Expr *highExpr = cast_node<Expr>(highNode);

    llvm::APInt lowValue;
    llvm::APInt highValue;
    if (!ensureStaticIntegerExpr(lowExpr, lowValue) or
        !ensureStaticIntegerExpr(highExpr, highValue))
        return;

    // Sign extend the values so that they have identical widths.
    unsigned lowWidth = lowValue.getBitWidth();
    unsigned highWidth = highValue.getBitWidth();
    if (lowWidth < highWidth)
        lowValue.sext(highWidth);
    else if (highWidth < lowWidth)
        highValue.sext(lowWidth);

    // Obtain a uniqued integer type to represent the base type of this
    // declaration and release the range expressions as they are now owned by
    // this new declaration.
    lowNode.release();
    highNode.release();
    IntegerType *intTy = resource.getIntegerType(lowValue, highValue);
    IntegerDecl *Idecl =
        new IntegerDecl(name, loc, lowExpr, highExpr, intTy, region);

    if (Decl *conflict = scope->addDirectDecl(Idecl)) {
        report(loc, diag::CONFLICTING_DECLARATION)
            << name << getSourceLoc(conflict->getLocation());
        return;
    }
    region->addDecl(Idecl);
    declProducer->createImplicitDecls(Idecl);
    importDeclRegion(Idecl);
}

bool TypeCheck::checkType(Type *source, SigInstanceDecl *target, Location loc)
{
    if (DomainType *domain = dyn_cast<DomainType>(source)) {
        if (!has(domain, target)) {
            report(loc, diag::DOES_NOT_SATISFY)
                << domain->getString()  << target->getString();
            return false;
        }
        return true;
    }

    if (CarrierType *carrier = dyn_cast<CarrierType>(source)) {
        DomainType *rep =
            dyn_cast<DomainType>(carrier->getRepresentationType());
        if (!rep) {
            report(loc, diag::NOT_A_DOMAIN);
            return false;
        }
        if (!has(rep, target)) {
            report(loc, diag::DOES_NOT_SATISFY)
                << carrier->getString() << target->getString();
            return false;
        }
        return true;
    }

    // Otherwise, the source does not denote a domain, and so cannot satisfy the
    // signature constraint.
    report(loc, diag::NOT_A_DOMAIN);
    return false;
}

bool TypeCheck::checkSignatureProfile(const AstRewriter &rewrites,
                                      Type *source, AbstractDomainDecl *target,
                                      Location loc)
{
    if (DomainType *domain = dyn_cast<DomainType>(source)) {
        if (!has(rewrites, domain, target)) {
            report(loc, diag::DOES_NOT_SATISFY)
                << domain->getString()  << target->getString();
            return false;
        }
        return true;
    }

    if (CarrierType *carrier = dyn_cast<CarrierType>(source)) {
        Type *rep = dyn_cast<DomainType>(carrier->getRepresentationType());
        return checkSignatureProfile(rewrites, rep, target, loc);
    }

    // Otherwise, the source does not denote a domain, and so cannot satisfy the
    // signature constraint.
    report(loc, diag::NOT_A_DOMAIN);
    return false;
}

bool TypeCheck::ensureExportConstraints(AddDecl *add)
{
    Domoid *domoid = add->getImplementedDomoid();
    IdentifierInfo *domainName = domoid->getIdInfo();
    PercentDecl *percent = domoid->getPercent();
    Location domainLoc = domoid->getLocation();

    bool allOK = true;

    // The domoid contains all of the declarations inherited from the super
    // signatures and any associated with expression.  Traverse the set of
    // declarations and ensure that the AddDecl provides a definition.
    for (DeclRegion::ConstDeclIter iter = percent->beginDecls();
         iter != percent->endDecls(); ++iter) {
        Decl *decl = *iter;
        Type *target = 0;

        // Extract the associated type from this decl.
        if (SubroutineDecl *routineDecl = dyn_cast<SubroutineDecl>(decl))
            target = routineDecl->getType();
        else if (ValueDecl *valueDecl = dyn_cast<ValueDecl>(decl))
            target = valueDecl->getType();

        // FIXME: We need a better diagnostic here.  In particular, we should be
        // reporting which signature(s) demand the missing export.  However, the
        // current organization makes this difficult.  One solution is to link
        // declaration nodes with those provided by the original signature
        // definition.
        if (target) {
            Decl *candidate = add->findDecl(decl->getIdInfo(), target);
            SubroutineDecl *srDecl = dyn_cast_or_null<SubroutineDecl>(candidate);
            if (!candidate || (srDecl && !srDecl->hasBody())) {
                report(domainLoc, diag::MISSING_EXPORT)
                    << domainName << decl->getIdInfo();
                allOK = false;
            }
            // Check that the parameter mode profiles match in the case of a
            // subroutine.
            if (srDecl) {
                SubroutineDecl *targetRoutine = cast<SubroutineDecl>(decl);
                if (!ensureMatchingParameterModes(srDecl, targetRoutine))
                    allOK = false;
            }
        }
    }
    return allOK;
}

void TypeCheck::importDeclRegion(DeclRegion *region)
{
    // FIXME: We should be able to import a region directly into a scope, thus
    // making these declarations indirect.  However, we do not have appropriate
    // scope API's yet.
    typedef DeclRegion::DeclIter iterator;

    for (iterator I = region->beginDecls(); I != region->endDecls(); ++I) {
        Decl *decl = *I;
        if (Decl *conflict = scope->addDirectDecl(decl)) {
            report(decl->getLocation(), diag::CONFLICTING_DECLARATION)
                << decl->getIdInfo() << getSourceLoc(conflict->getLocation());
        }
    }
}

/// Returns true if the given parameter is of mode "in", and thus capatable with
/// a function declaration.  Otherwise false is returned an a diagnostic is
/// posted.
bool TypeCheck::checkFunctionParameter(ParamValueDecl *param)
{
    PM::ParameterMode mode = param->getParameterMode();
    if (mode == PM::MODE_IN)
        return true;
    report(param->getLocation(), diag::OUT_MODE_IN_FUNCTION);
    return false;
}

/// Returns true if the given descriptor does not contain any duplicate formal
/// parameters.  Otherwise false is returned and the appropriate diagnostic is
/// posted.
bool TypeCheck::checkDescriptorDuplicateParams(Descriptor &desc)
{
    typedef Descriptor::paramIterator iterator;

    bool status = true;
    iterator I = desc.beginParams();
    iterator E = desc.endParams();
    while (I != E) {
        Decl *p1 = cast_node<Decl>(*I);
        for (iterator J = ++I; J != E; ++J) {
            Decl *p2 = cast_node<Decl>(*J);
            if (p1->getIdInfo() == p2->getIdInfo()) {
                report(p2->getLocation(), diag::DUPLICATE_FORMAL_PARAM)
                    << p2->getString();
                status = false;
            }
        }
    }
    return status;
}

/// Returns true if the descriptor does not contain any parameters with the
/// given name.  Otherwise false is returned and the appropriate diagnostic is
/// posted.
bool TypeCheck::checkDescriptorDuplicateParams(Descriptor &desc,
                                               IdentifierInfo *idInfo,
                                               Location loc)
{
    typedef Descriptor::paramIterator iterator;

    iterator I = desc.beginParams();
    iterator E = desc.endParams();
    for ( ; I != E; ++I) {
        Decl *param = cast_node<Decl>(*I);
        if (param->getIdInfo() == idInfo) {
            report(loc, diag::DUPLICATE_FORMAL_PARAM) << idInfo;
            return false;
        }
    }
    return true;
}

/// Returns true if the IdentifierInfo \p info can name a binary function.
bool TypeCheck::namesBinaryFunction(IdentifierInfo *info)
{
    const char* name = info->getString();
    size_t length = std::strlen(name);

    if (length > 2)
        return false;

    if (length == 1) {
        switch (*name) {
        default:
            return false;
        case '=':
        case '+':
        case '*':
        case '-':
        case '>':
        case '<':
            return true;
        }
    }
    else
        return (std::strncmp(name, "<=", 2) or
                std::strncmp(name, ">=", 2));
}
