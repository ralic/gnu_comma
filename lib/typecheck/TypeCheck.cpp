//===-- typecheck/TypeCheck.cpp ------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008-2009 Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "RangeChecker.h"
#include "Scope.h"
#include "Stencil.h"
#include "TypeCheck.h"
#include "comma/ast/AstRewriter.h"
#include "comma/ast/AttribExpr.h"
#include "comma/ast/ExceptionRef.h"
#include "comma/ast/Expr.h"
#include "comma/ast/Decl.h"
#include "comma/ast/DSTDefinition.h"
#include "comma/ast/KeywordSelector.h"
#include "comma/ast/Pragma.h"
#include "comma/ast/RangeAttrib.h"
#include "comma/ast/Stmt.h"
#include "comma/ast/TypeRef.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"

#include <algorithm>
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
      compUnit(cunit)
{
    populateInitialEnvironment();
}

TypeCheck::~TypeCheck() { }

// Called when then type checker is constructed.  Populates the top level scope
// with an initial environment.
void TypeCheck::populateInitialEnvironment()
{
    EnumerationDecl *theBoolDecl = resource.getTheBooleanDecl();
    scope.addDirectDecl(theBoolDecl);
    introduceImplicitDecls(theBoolDecl);

    // We do not add root_integer into scope, since it is an anonymous language
    // defined type.
    IntegerDecl *theRootIntegerDecl = resource.getTheRootIntegerDecl();
    introduceImplicitDecls(theRootIntegerDecl);

    IntegerDecl *theIntegerDecl = resource.getTheIntegerDecl();
    scope.addDirectDecl(theIntegerDecl);
    introduceImplicitDecls(theIntegerDecl);

    // Positive and Natural are subtypes of Integer, and so do not export
    // any additional declarations.
    IntegerSubtypeDecl *thePositiveDecl = resource.getThePositiveDecl();
    scope.addDirectDecl(thePositiveDecl);
    IntegerSubtypeDecl *theNaturalDecl = resource.getTheNaturalDecl();
    scope.addDirectDecl(theNaturalDecl);

    EnumerationDecl *theCharacterDecl = resource.getTheCharacterDecl();
    scope.addDirectDecl(theCharacterDecl);
    introduceImplicitDecls(theCharacterDecl);

    ArrayDecl *theStringDecl = resource.getTheStringDecl();
    scope.addDirectDecl(theStringDecl);

    // Add the standard exception objects into scope.
    scope.addDirectDecl(resource.getTheProgramError());
    scope.addDirectDecl(resource.getTheConstraintError());
}

void TypeCheck::deleteNode(Node &node)
{
    Ast *ast = lift_node<Ast>(node);
    if (ast && ast->isDeletable())
        delete ast;
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

Node TypeCheck::acceptPercent(Location loc)
{
    TypeRef *ref = 0;

    // We are either processing a model or a generic formal domain.
    //
    // When processing a model, return the associated percent decl.  When
    // processing a generic formal domain, return the AbstractDomainDecl.
    if (ModelDecl *model = getCurrentModel())
        ref = new TypeRef(loc, model->getPercent());
    else {
        // FIXME: Use a cleaner interface when available.
        AbstractDomainDecl *decl = cast<AbstractDomainDecl>(declarativeRegion);
        ref = new TypeRef(loc, decl);
    }
    return getNode(ref);
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
        if (candidate && domain)
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
/// It is assumed that the actual arguments provided are compatible with the
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
        rewriter.addTypeRewrite(formal, actual);
    }

    SigInstanceDecl *target = parameterizedModel->getFormalSignature(numArguments);
    return rewriter.rewriteSigInstance(target);
}

// Ensures that the given TypeRef is of a sort compatible with the
// parameterization of a variety or functor (e.g. the TypeRef resolves to a
// DomainTypeDecl).  Returns the resolved DomainTypeDecl on sucess.  Otherwise
// diagnostics are posted and null is returned.
DomainTypeDecl *TypeCheck::ensureValidModelParam(TypeRef *ref)
{
    TypeDecl *arg = ref->getTypeDecl();
    DomainTypeDecl *dom = dyn_cast_or_null<DomainTypeDecl>(arg);
    if (!dom) {
        Location loc = ref->getLocation();
        report(loc, diag::INVALID_TYPE_PARAM) << ref->getIdInfo();
    }
    return dom;
}

TypeRef *
TypeCheck::acceptTypeApplication(TypeRef *ref,
                                 SVImpl<TypeRef*>::Type &posArgs,
                                 SVImpl<KeywordSelector*>::Type &keyedArgs)
{
    Location loc = ref->getLocation();
    IdentifierInfo *name = ref->getIdInfo();

    // If the type reference is complete, we cannot apply any arguments.
    if (ref->isComplete()) {
        report(loc, diag::WRONG_NUM_ARGS_FOR_TYPE) << name;
        return 0;
    }

    ModelDecl *model = ref->getModelDecl();
    unsigned numPositional = posArgs.size();
    unsigned numKeyed = keyedArgs.size();
    unsigned numArgs = numPositional + numKeyed;

    if (model->getArity() != numArgs) {
        report(loc, diag::WRONG_NUM_ARGS_FOR_TYPE) << name;
        return 0;
    }

    // Check that the model accepts the given keywords.
    if (!checkModelKeywordArgs(model, numPositional, keyedArgs))
        return 0;

    // Build the a sorted vector of arguments, checking that each type reference
    // denotes a domain.  Similarly build a vector of sorted locations for each
    // argument.
    llvm::SmallVector<DomainTypeDecl *, 8> args(numArgs);
    llvm::SmallVector<Location, 8> argLocs(numArgs);

    // Process the positional parameters.
    for (unsigned i = 0; i < numPositional; ++i) {
        DomainTypeDecl *dom = ensureValidModelParam(posArgs[i]);
        if (!dom)
            return 0;
        args[i] = dom;
        argLocs[i] = posArgs[i]->getLocation();
    }

    // Process the keyed parameters, placing them in their sorted positions
    // (checkModelKeywordArgs has already assured us this mapping will not
    // conflict).
    for (unsigned i = 0; i < numKeyed; ++i) {
        KeywordSelector *selector = keyedArgs[i];
        TypeRef *argRef = keyedArgs[i]->getTypeRef();
        DomainTypeDecl *dom = ensureValidModelParam(argRef);

        if (!dom)
            return 0;

        IdentifierInfo *key = selector->getKeyword();
        unsigned index = unsigned(model->getKeywordIndex(key));
        args[index] = dom;
        argLocs[index] = argRef->getLocation();
    }

    // Check that the arguments satisfy the parameterization constraints of the
    // model.
    if (!checkModelArgs(model, args, argLocs))
        return 0;


    // Obtain a memoized type node for this particular argument set.
    TypeRef *instanceRef = 0;
    if (VarietyDecl *V = dyn_cast<VarietyDecl>(model)) {
        SigInstanceDecl *instance = V->getInstance(&args[0], numArgs);
        instanceRef = new TypeRef(loc, instance);
    }
    else {
        FunctorDecl *F = cast<FunctorDecl>(model);

        // Ensure the requested instance is not self recursive.
        if (!ensureNonRecursiveInstance(F, &args[0], numArgs, loc))
            return false;

        // If this particular functor parameterization is equivalent to %, warn
        // and canonicalize to the unique percent node.
        if (denotesFunctorPercent(F, &args[0], numArgs)) {
            report(loc, diag::PERCENT_EQUIVALENT);
            instanceRef = new TypeRef(loc, getCurrentPercent());
        }
        else {
            DomainInstanceDecl *instance;
            instance = F->getInstance(&args[0], numArgs);
            instanceRef = new TypeRef(loc, instance);
        }
    }
    return instanceRef;
}

bool TypeCheck::checkModelArgs(ModelDecl *model,
                               SVImpl<DomainTypeDecl*>::Type &args,
                               SVImpl<Location>::Type &argLocs)
{
    AstRewriter rewrites(resource);
    unsigned numArgs = args.size();
    for (unsigned i = 0; i < numArgs; ++i) {
        DomainType *argTy = args[i]->getType();
        Location argLoc = argLocs[i];
        AbstractDomainDecl *target = model->getFormalDecl(i);

        // Extend the rewriter mapping the formal argument type to the type of
        // the actual argument.
        rewrites.addTypeRewrite(target->getType(), argTy);

        // Check the argument in the using the rewriter as context.
        if (!checkSignatureProfile(rewrites, argTy, target, argLoc))
            return false;
    }

    return true;
}

bool TypeCheck::checkModelKeywordArgs(ModelDecl *model, unsigned numPositional,
                                      SVImpl<KeywordSelector*>::Type &keyedArgs)
{
    unsigned numKeys = keyedArgs.size();
    for (unsigned i = 0; i < numKeys; ++i) {
        KeywordSelector *selector = keyedArgs[i];
        IdentifierInfo *keyword = selector->getKeyword();
        Location keywordLoc = selector->getLocation();
        int keywordIdx = model->getKeywordIndex(keyword);

        // Ensure the given keyword exists.
        if (keywordIdx < 0) {
            report(keywordLoc, diag::TYPE_HAS_NO_SUCH_KEYWORD)
                << keyword << model->getIdInfo();
            return false;
        }

        // The corresponding index of the keyword must be greater than the
        // number of supplied positional parameters (otherwise it would
        // `overlap' a positional parameter).
        if ((unsigned)keywordIdx < numPositional) {
            report(keywordLoc, diag::PARAM_PROVIDED_POSITIONALLY) << keyword;
            return false;
        }

        // Ensure that this keyword is not a duplicate of any preceeding
        // keyword.
        for (unsigned j = 0; j < i; ++j) {
            if (keyedArgs[j]->getKeyword() == keyword) {
                report(keywordLoc, diag::DUPLICATE_KEYWORD) << keyword;
                return false;
            }
        }
    }
    return true;
}

TypeDecl *TypeCheck::ensureTypeDecl(Decl *decl, Location loc, bool report)
{
    if (TypeDecl *tyDecl = dyn_cast<TypeDecl>(decl))
        return tyDecl;
    if (report)
        this->report(loc, diag::NOT_A_TYPE);
    return 0;
}

TypeDecl *TypeCheck::ensureTypeDecl(Node node, bool report)
{
    if (TypeRef *ref = lift_node<TypeRef>(node)) {
        return ensureTypeDecl(ref->getDecl(), ref->getLocation(), report);
    }
    else if (report) {
        this->report(getNodeLoc(node), diag::NOT_A_TYPE);
    }
    return 0;
}

bool TypeCheck::ensureStaticIntegerExpr(Expr *expr, llvm::APInt &result)
{
    if (isa<IntegerType>(expr->getType()) &&
        expr->staticDiscreteValue(result))
        return true;

    report(expr->getLocation(), diag::NON_STATIC_EXPRESSION);
    return false;
}

bool TypeCheck::ensureStaticIntegerExpr(Expr *expr)
{
    if (isa<IntegerType>(expr->getType()) &&
        expr->isStaticDiscreteExpr())
        return true;

    report(expr->getLocation(), diag::NON_STATIC_EXPRESSION);
    return false;
}

ArrayType *TypeCheck::getConstrainedArraySubtype(ArrayType *arrTy, Expr *init)
{
    // FIXME: The following code assumes integer index types exclusively.
    // FIXME: Support multidimensional array types.
    assert(!arrTy->isConstrained() && "Array type already constrained!");
    assert(arrTy->getRank() == 1 && "Multidimensional arrays not supported!");

    if (StringLiteral *strLit = dyn_cast<StringLiteral>(init)) {
        unsigned length = strLit->length();
        DiscreteType *idxTy = cast<DiscreteType>(arrTy->getIndexType(0));

        // FIXME:  Support null string literals by generating a null index type.
        assert(length != 0 && "Null string literals not yet supported!");

        // Obtain the lower and upper limits for the index type and ensure that
        // the given literal is representable within those bounds.
        llvm::APInt lower;
        llvm::APInt upper;

        if (const Range *range = idxTy->getConstraint()) {
            assert(range->isStatic() && "FIXME: Support dynamic indices.");
            lower = range->getStaticLowerBound();
            upper = range->getStaticUpperBound();
        }
        else {
            // Use the representational limits.
            idxTy->getLowerLimit(lower);
            idxTy->getUpperLimit(upper);
        }

        // The following subtraction is always valid provided we treat the
        // result as unsigned.  Note that the value computed here is one less
        // than the actual cardinality -- this is to avoid overflow.
        uint64_t cardinality = (upper - lower).getZExtValue();

        // Reduce the non-zero length by one to fit the "zero based" cardinality
        // value.
        --length;

        if (length > cardinality) {
            report(init->getLocation(), diag::TOO_MANY_ELEMENTS_FOR_TYPE)
                << arrTy->getIdInfo();
            return 0;
        }

        // Adjust the upper bound to the length of the literal.
        upper = length;
        upper += lower;

        // Generate expressions for the bounds.
        //
        // FIXME: Support enumeration types by generating Val attribute
        // expressions.
        IntegerType *intTy = cast<IntegerType>(idxTy);
        Expr *lowerExpr = new IntegerLiteral(lower, idxTy, 0);
        Expr *upperExpr = new IntegerLiteral(upper, idxTy, 0);
        idxTy = resource.createIntegerSubtype(0, intTy, lowerExpr, upperExpr);
        return resource.createArraySubtype(0, arrTy, &idxTy);
    }

    ArrayType *exprTy = cast<ArrayType>(init->getType());

    // Check that both root types are identical.
    if (exprTy->getRootType() != arrTy->getRootType()) {
        report(init->getLocation(), diag::INCOMPATIBLE_TYPES);
        return 0;
    }

    // If the expression type is statically constrained, propogate the
    // expression's type.  Otherwise, leave the type as unconstrained.
    if (exprTy->isStaticallyConstrained())
        return exprTy;
    return arrTy;
}

ObjectDecl *TypeCheck::acceptArrayObjectDeclaration(Location loc,
                                                    IdentifierInfo *name,
                                                    ArrayDecl *arrDecl,
                                                    Expr *init)
{
    ArrayType *arrTy = arrDecl->getType();

    if (!arrTy->isConstrained() && (init == 0)) {
        report(loc, diag::UNCONSTRAINED_ARRAY_OBJECT_REQUIRES_INIT);
        return 0;
    }

    if (init && !(init = checkExprInContext(init, arrTy)))
        return 0;

    // If the array type is unconstrained, use the resolved type of the
    // initializer.
    if (!arrTy->isConstrained())
        arrTy = cast<ArrayType>(init->getType());

    return new ObjectDecl(name, arrTy, loc, init);
}

bool TypeCheck::acceptObjectDeclaration(Location loc, IdentifierInfo *name,
                                        Node refNode, Node initializerNode)
{
    Expr *init = 0;
    ObjectDecl *decl = 0;
    TypeDecl *tyDecl = ensureTypeDecl(refNode);

    if (!tyDecl) return false;

    if (!initializerNode.isNull())
        init = ensureExpr(initializerNode);

    if (ArrayDecl *arrDecl = dyn_cast<ArrayDecl>(tyDecl)) {
        decl = acceptArrayObjectDeclaration(loc, name, arrDecl, init);
        if (decl == 0)
            return false;
    }
    else {
        Type *objTy = tyDecl->getType();
        if (init) {
            init = checkExprInContext(init, objTy);
            if (!init)
                return false;
        }
        decl = new ObjectDecl(name, objTy, loc, init);
    }


    initializerNode.release();
    refNode.release();

    if (Decl *conflict = scope.addDirectDecl(decl)) {
        SourceLocation sloc = getSourceLoc(conflict->getLocation());
        report(loc, diag::DECLARATION_CONFLICTS) << name << sloc;
        return false;
    }
    currentDeclarativeRegion()->addDecl(decl);
    return true;
}

bool TypeCheck::acceptImportDeclaration(Node importedNode)
{
    TypeRef *ref = cast_node<TypeRef>(importedNode);
    Decl *decl = ref->getDecl();
    Location loc = ref->getLocation();
    DomainType *domain;

    if (CarrierDecl *carrier = dyn_cast<CarrierDecl>(decl))
        domain = dyn_cast<DomainType>(carrier->getRepresentationType());
    else
        domain = dyn_cast<DomainTypeDecl>(decl)->getType();

    if (!domain) {
        report(loc, diag::IMPORT_FROM_NON_DOMAIN);
        return false;
    }

    scope.addImport(domain);

    // FIXME:  We need to stitch this import declaration into the current
    // context.
    new ImportDecl(domain, loc);
    return true;
}

void TypeCheck::beginEnumeration(IdentifierInfo *name, Location loc)
{
    enumStencil.init(name, loc);
}

void TypeCheck::acceptEnumerationIdentifier(IdentifierInfo *name, Location loc)
{
    acceptEnumerationLiteral(name, loc);
}

void TypeCheck::acceptEnumerationCharacter(IdentifierInfo *name, Location loc)
{
    if (acceptEnumerationLiteral(name, loc))
        enumStencil.markAsCharacterType();
}

bool TypeCheck::acceptEnumerationLiteral(IdentifierInfo *name, Location loc)
{
    // Check that the given element name has yet to appear in the set of
    // elements.  If it exists, mark the stencil as invalid and ignore the
    // element.
    EnumDeclStencil::elem_iterator I = enumStencil.begin_elems();
    EnumDeclStencil::elem_iterator E = enumStencil.end_elems();
    for ( ; I != E; ++I) {
        if (I->first == name) {
            enumStencil.markInvalid();
            report(loc, diag::MULTIPLE_ENUMERATION_LITERALS) << name;
            return false;
        }
    }

    // Check that the element does not conflict with the name of the enumeration
    // decl itself.
    if (name == enumStencil.getIdInfo()) {
        report(loc, diag::CONFLICTING_DECLARATION)
            << name << getSourceLoc(enumStencil.getLocation());
        return false;
    }

    enumStencil.addElement(name, loc);
    return true;
}

void TypeCheck::endEnumeration()
{
    IdentifierInfo *name = enumStencil.getIdInfo();
    Location loc = enumStencil.getLocation();
    DeclRegion *region = currentDeclarativeRegion();
    EnumDeclStencil::IdLocPair *elems = enumStencil.getElements().data();
    unsigned numElems = enumStencil.numElements();
    EnumerationDecl *decl;

    ASTStencilReseter reseter(enumStencil);

    // It is possible that the enumeration is empty due to previous errors.  Do
    // not even bother constructing such malformed nodes.
    if (!numElems)
        return;

    decl = resource.createEnumDecl(name, loc, elems, numElems, region);

    // Check that the enumeration does not conflict with any other in the
    // current scope.
    if (Decl *conflict = scope.addDirectDecl(decl)) {
        report(loc, diag::CONFLICTING_DECLARATION)
            << name << getSourceLoc(conflict->getLocation());
        return;
    }

    // Mark the declaration as a character type if any character literals were
    // used to define it.
    if (enumStencil.isCharacterType())
        decl->markAsCharacterType();

    region->addDecl(decl);
    decl->generateImplicitDeclarations(resource);
    introduceImplicitDecls(decl);
}

void TypeCheck::acceptIntegerTypeDecl(IdentifierInfo *name, Location loc,
                                      Node lowNode, Node highNode)
{
    DeclRegion *region = currentDeclarativeRegion();
    Expr *lower = cast_node<Expr>(lowNode);
    Expr *upper = cast_node<Expr>(highNode);
    RangeChecker rangeCheck(*this);

    if (!rangeCheck.checkDeclarationRange(lower, upper))
        return;

    // Obtain an integer type to represent the base type of this declaration and
    // release the range expressions as they are now owned by this new
    // declaration.
    lowNode.release();
    highNode.release();
    IntegerDecl *decl;
    decl = resource.createIntegerDecl(name, loc, lower, upper, region);

    if (Decl *conflict = scope.addDirectDecl(decl)) {
        report(loc, diag::CONFLICTING_DECLARATION)
            << name << getSourceLoc(conflict->getLocation());
        return;
    }

    region->addDecl(decl);
    decl->generateImplicitDeclarations(resource);
    introduceImplicitDecls(decl);
}

void TypeCheck::acceptRangedSubtypeDecl(IdentifierInfo *name, Location loc,
                                        Node subtypeNode,
                                        Node lowNode, Node highNode)
{
    DeclRegion *region = currentDeclarativeRegion();

    TypeRef *tyRef = lift_node<TypeRef>(subtypeNode);
    if (!tyRef) {
        report(getNodeLoc(subtypeNode), diag::DOES_NOT_DENOTE_A_TYPE);
        return;
    }

    TypeDecl *tyDecl = tyRef->getTypeDecl();
    if (!tyDecl) {
        report(tyRef->getLocation(), diag::EXPECTED_DISCRETE_SUBTYPE);
        return;
    }

    DiscreteType *baseTy = dyn_cast<DiscreteType>(tyDecl->getType());
    if (!baseTy) {
        report(tyRef->getLocation(), diag::EXPECTED_DISCRETE_SUBTYPE);
        return;
    }

    // Convert each of the constraints to the expressions and evaluate them in
    // the context of the subtype indication.
    Expr *lower = ensureExpr(lowNode);
    Expr *upper = ensureExpr(highNode);

    if (!(lower = checkExprInContext(lower, baseTy)) ||
        !(upper = checkExprInContext(upper, baseTy)))
        return;

    // Construct the specific subtype declaration.
    Decl *decl;

    switch (baseTy->getKind()) {
    default:
        assert(false && "Unexpected discrete type!");
        decl = 0;

    case Ast::AST_IntegerType: {
        IntegerType *intTy = cast<IntegerType>(baseTy);
        decl = resource.createIntegerSubtypeDecl(
            name, loc, intTy, lower, upper, region);
        break;
    }

    case Ast::AST_EnumerationType: {
        EnumerationType *enumTy = cast<EnumerationType>(baseTy);
        decl = resource.createEnumSubtypeDecl(
            name, loc, enumTy, lower, upper, region);
        break;
    }
    }

    subtypeNode.release();
    lowNode.release();
    highNode.release();

    if (Decl *conflict = scope.addDirectDecl(decl)) {
        report(loc, diag::CONFLICTING_DECLARATION)
            << name << getSourceLoc(conflict->getLocation());
        return;
    }
    region->addDecl(decl);
}

void TypeCheck::acceptSubtypeDecl(IdentifierInfo *name, Location loc,
                                  Node subtypeNode)
{
    DeclRegion *region = currentDeclarativeRegion();
    TypeRef *subtype = lift_node<TypeRef>(subtypeNode);

    if (!subtype) {
        report(getNodeLoc(subtypeNode), diag::DOES_NOT_DENOTE_A_TYPE);
        return;
    }

    /// FIXME: The only kind of unconstrained subtype declarations we currently
    /// support are discrete subtypes.
    DiscreteType *baseTy = 0;

    if (TypeDecl *tyDecl = subtype->getTypeDecl()) {
        baseTy = dyn_cast<DiscreteType>(tyDecl->getType());
    }

    if (!baseTy) {
        report(subtype->getLocation(), diag::INVALID_SUBTYPE_INDICATION);
        return;
    }

    Decl *decl = 0;

    switch (baseTy->getKind()) {
    default:
        assert(false && "Unexpected subtype indication!");
        break;

    case Ast::AST_IntegerType: {
        IntegerType *intTy = cast<IntegerType>(baseTy);
        decl = resource.createIntegerSubtypeDecl(name, loc, intTy, region);
        break;
    }

    case Ast::AST_EnumerationType : {
        EnumerationType *enumTy = cast<EnumerationType>(baseTy);
        decl = resource.createEnumSubtypeDecl(name, loc, enumTy, region);
        break;
    }
    }

    subtypeNode.release();
    if (Decl *conflict = scope.addDirectDecl(decl)) {
        report(loc, diag::CONFLICTING_DECLARATION)
            << name << getSourceLoc(conflict->getLocation());
        return;
    }
    region->addDecl(decl);
}

void TypeCheck::acceptArrayDecl(IdentifierInfo *name, Location loc,
                                NodeVector indexNodes, Node componentNode)
{
    assert(!indexNodes.empty() && "No type indices for array type decl!");

    // Build a vector of the DSTDefinition's describing the indices of this
    // array declaration.
    typedef NodeCaster<DSTDefinition> Caster;
    typedef llvm::mapped_iterator<NodeVector::iterator, Caster> Mapper;
    typedef llvm::SmallVector<DSTDefinition*, 8> IndexVec;
    IndexVec indices(Mapper(indexNodes.begin(), Caster()),
                     Mapper(indexNodes.end(), Caster()));

    // Unfortunately the parser does not ensure that all index types are either
    // constrained or unconstrained.  Determine the nature of this array
    // declaration by inspecting the first index, then check that every
    // subsequent index follows the rules.
    DSTDefinition::DSTTag tag = indices[0]->getTag();
    bool isConstrained = tag != DSTDefinition::Unconstrained_DST;
    bool allOK = true;
    for (IndexVec::iterator I = indices.begin(); I != indices.end(); ++I) {
        DSTDefinition *index = *I;
        DSTDefinition::DSTTag tag = index->getTag();
        if (tag == DSTDefinition::Unconstrained_DST && isConstrained) {
            report(index->getLocation(),
                   diag::EXPECTED_CONSTRAINED_ARRAY_INDEX);
            allOK = false;
        }
        else if (tag != DSTDefinition::Unconstrained_DST && !isConstrained) {
            report(index->getLocation(),
                   diag::EXPECTED_UNCONSTRAINED_ARRAY_INDEX);
            allOK = false;
        }
    }

    if (!allOK)
        return;

    // Ensure the component node is in fact a type.
    PrimaryType *componentTy;
    if (TypeDecl *componentDecl = ensureTypeDecl(componentNode))
        componentTy = componentDecl->getType();
    else
        return;

    // Create the declaration node.
    DeclRegion *region = currentDeclarativeRegion();
    ArrayDecl *array =
        resource.createArrayDecl(name, loc, indices.size(), &indices[0],
                                 componentTy, isConstrained, region);

    // Check for conflicts.
    if (Decl *conflict = scope.addDirectDecl(array)) {
        report(loc, diag::CONFLICTING_DECLARATION)
            << name << getSourceLoc(conflict->getLocation());
        return;
    }

    // FIXME: We need to introduce the implicit operations for this type.
    region->addDecl(array);
    introduceImplicitDecls(array);
}

//===----------------------------------------------------------------------===//
// DSTDefinition callbacks.

Node TypeCheck::acceptDSTDefinition(Node name, Node lowerNode, Node upperNode)
{
    TypeRef *ref = lift_node<TypeRef>(name);
    DiscreteType *subtype = 0;

    if (ref) {
        if (TypeDecl *decl = ref->getTypeDecl()) {
            subtype = dyn_cast<DiscreteType>(decl->getType());
        }
    }

    if (!subtype) {
        report(getNodeLoc(name), diag::EXPECTED_DISCRETE_SUBTYPE_OR_RANGE);
        return getInvalidNode();
    }

    Expr *lower = ensureExpr(lowerNode);
    Expr *upper = ensureExpr(upperNode);
    if (!(lower && upper))
        return getInvalidNode();

    subtype = RangeChecker(*this).checkSubtypeRange(subtype, lower, upper);
    if (!subtype)
        return getInvalidNode();

    DSTDefinition::DSTTag tag = DSTDefinition::Constrained_DST;
    DSTDefinition *result = new DSTDefinition(ref->getLocation(), subtype, tag);

    name.release();
    lowerNode.release();
    upperNode.release();
    delete ref;
    return getNode(result);
}

Node TypeCheck::acceptDSTDefinition(Node nameOrAttribute, bool isUnconstrained)
{
    DSTDefinition *result = 0;

    if (TypeRef *ref = lift_node<TypeRef>(nameOrAttribute)) {
        if (TypeDecl *decl = ref->getTypeDecl()) {
            if (DiscreteType *type = dyn_cast<DiscreteType>(decl->getType())) {
                DSTDefinition::DSTTag tag = isUnconstrained ?
                    DSTDefinition::Unconstrained_DST : DSTDefinition::Type_DST;
                result = new DSTDefinition(ref->getLocation(), type, tag);
                delete ref;
            }
        }
    }
    else if (RangeAttrib *attrib = lift_node<RangeAttrib>(nameOrAttribute)) {
        DSTDefinition::DSTTag tag = DSTDefinition::Attribute_DST;
        result = new DSTDefinition(attrib->getLocation(), attrib, tag);
    }

    if (!result) {
        report(getNodeLoc(nameOrAttribute),
               diag::EXPECTED_DISCRETE_SUBTYPE_OR_RANGE);
        return getInvalidNode();
    }

    nameOrAttribute.release();
    return getNode(result);
}

Node TypeCheck::acceptDSTDefinition(Node lowerNode, Node upperNode)
{
    Expr *lower = ensureExpr(lowerNode);
    Expr *upper = ensureExpr(upperNode);
    RangeChecker rangeCheck(*this);
    DiscreteType *subtype = 0;

    if (!(lower && upper))
        return getInvalidNode();

    if (!(subtype = rangeCheck.checkDSTRange(lower, upper)))
        return getInvalidNode();

    lowerNode.release();
    upperNode.release();
    DSTDefinition::DSTTag tag = DSTDefinition::Range_DST;
    return getNode(new DSTDefinition(lower->getLocation(), subtype, tag));
}

bool TypeCheck::checkSignatureProfile(const AstRewriter &rewrites,
                                      Type *source, AbstractDomainDecl *target,
                                      Location loc)
{
    if (DomainType *domain = dyn_cast<DomainType>(source)) {
        if (!has(rewrites, domain, target)) {
            report(loc, diag::DOMAIN_PARAM_DOES_NOT_SATISFY)
                << target->getString();
            return false;
        }
        return true;
    }

    // Otherwise, the source does not denote a domain, and so cannot satisfy the
    // signature constraint.
    report(loc, diag::NOT_A_DOMAIN);
    return false;
}

void TypeCheck::introduceImplicitDecls(DeclRegion *region)
{
    typedef DeclRegion::DeclIter iterator;
    for (iterator I = region->beginDecls(); I != region->endDecls(); ++I) {
        Decl *decl = *I;
        if (Decl *conflict = scope.addDirectDecl(decl)) {
            report(decl->getLocation(), diag::CONFLICTING_DECLARATION)
                << decl->getIdInfo() << getSourceLoc(conflict->getLocation());
        }
    }
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
        return (std::strncmp(name, "<=", 2) == 0 ||
                std::strncmp(name, ">=", 2) == 0);
}

bool TypeCheck::namesUnaryFunction(IdentifierInfo *info)
{
    const char* name = info->getString();
    size_t length = std::strlen(name);

    if (length == 1) {
        switch (*name) {
        default:
            return false;
        case '+':
        case '-':
            return true;
        }
    }
    else
        return false;
}

Location TypeCheck::getNodeLoc(Node node)
{
    assert(!node.isNull() && "Cannot get locations from null nodes!");
    assert(node.isValid() && "Cannot get locations from invalid nodes!");

    if (Expr *expr = lift_node<Expr>(node))
        return expr->getLocation();

    if (TypeRef *ref = lift_node<TypeRef>(node))
        return ref->getLocation();

    if (SubroutineRef *ref = lift_node<SubroutineRef>(node))
        return ref->getLocation();

    if (ExceptionRef *ref = lift_node<ExceptionRef>(node))
        return ref->getLocation();

    ProcedureCallStmt *call = cast_node<ProcedureCallStmt>(node);
    return call->getLocation();
}

bool TypeCheck::covers(Type *A, Type *B)
{
    // A type covers itself.
    if (A == B)
        return true;

    Type *rootTypeA = A;
    Type *rootTypeB = B;

    // If either A or B are primary, resolve their root types.
    if (PrimaryType *primary = dyn_cast<PrimaryType>(A))
        rootTypeA = primary->getRootType();
    if (PrimaryType *primary = dyn_cast<PrimaryType>(B))
        rootTypeB = primary->getRootType();

    return rootTypeA == rootTypeB;
}

bool TypeCheck::conversionRequired(Type *sourceTy, Type *targetTy)
{
    if (sourceTy == targetTy)
        return false;

    PrimaryType *source = dyn_cast<PrimaryType>(sourceTy);
    PrimaryType *target = dyn_cast<PrimaryType>(targetTy);

    if (!(source && target))
        return false;

    // If the source is a subtype of the target a conversion is not required.
    if (source->isSubtypeOf(target))
        return false;

    // If the target is an unconstrained subtype of a common base, a conversion
    // is not needed.
    if ((source->getRootType() == target->getRootType()) &&
        !target->isConstrained())
        return false;

    return true;
}

Expr *TypeCheck::convertIfNeeded(Expr *expr, Type *target)
{
    if (conversionRequired(expr->getType(), target))
        return new ConversionExpr(expr, target);
    return expr;
}

void TypeCheck::acceptPragmaImport(Location pragmaLoc,
                                   IdentifierInfo *convention,
                                   Location conventionLoc,
                                   IdentifierInfo *entity,
                                   Location entityLoc,
                                   Node externalNameNode)
{
    llvm::StringRef conventionRef(convention->getString());
    PragmaImport::Convention ID = PragmaImport::getConventionID(conventionRef);

    if (ID == PragmaImport::UNKNOWN_CONVENTION) {
        report(conventionLoc, diag::UNKNOWN_CONVENTION) << convention;
        return;
    }

    Resolver &resolver = scope.getResolver();
    if (!resolver.resolve(entity) || !resolver.hasDirectOverloads()) {
        report(entityLoc, diag::NAME_NOT_VISIBLE) << entity;
        return;
    }

    // We do not support importation of overloaded declarations (yet).
    if (resolver.numDirectOverloads() != 1) {
        report(entityLoc, diag::OVERLOADED_IMPORT_NOT_SUPPORTED);
        return;
    }

    // Resolve the external name to a static string expression.
    Expr *name = cast_node<Expr>(externalNameNode);
    if (!(name = checkExprInContext(name, resource.getTheStringType())))
        return;
    if (!name->isStaticStringExpr()) {
        report(name->getLocation(), diag::NON_STATIC_EXPRESSION);
        return;
    }

    // Ensure the declaration does not already have a import pragma associated
    // with it.
    SubroutineDecl *srDecl =
        cast<SubroutineDecl>(resolver.getDirectOverload(0));
    if (srDecl->hasPragma(pragma::Import)) {
        report(entityLoc, diag::DUPLICATE_IMPORT_PRAGMAS) << entity;
        return;
    }

    // The pragma checks out.
    //
    // FIXME: We should also ensure that the declaration profile is conformant
    // with the convention (does not involve unconstrained array types, for
    // example).
    externalNameNode.release();
    PragmaImport *pragma = new PragmaImport(pragmaLoc, ID, entity, name);
    srDecl->attachPragma(pragma);
}

PragmaAssert *TypeCheck::acceptPragmaAssert(Location loc, NodeVector &args)
{
    // Currently, assert pragmas take a single boolean valued argument.  The
    // parser knows this.
    assert(args.size() == 1 && "Wrong number of arguments for pragma Assert!");
    Expr *pred = cast_node<Expr>(args[0]);

    if ((pred = checkExprInContext(pred, resource.getTheBooleanType()))) {
        // Get a string representing the source location of the assertion.
        //
        // FIXME: We should be calling a utility routine to parse the source
        // location.
        std::string message;
        SourceLocation sloc = getSourceLoc(loc);
        llvm::raw_string_ostream stream(message);
        std::string identity = sloc.getTextProvider()->getIdentity();
        stream << "Assertion failed at "
               << identity << ":"
               << sloc.getLine() << ":" << sloc.getColumn() << ".\n";
        return new PragmaAssert(loc, pred, stream.str());
    }
    return 0;
}

Checker *Checker::create(Diagnostic      &diag,
                         AstResource     &resource,
                         CompilationUnit *cunit)
{
    return new TypeCheck(diag, resource, cunit);
}
