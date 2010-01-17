//===-- typecheck/CheckExpr.cpp ------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009-2010, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "Scope.h"
#include "TypeCheck.h"
#include "comma/ast/AggExpr.h"
#include "comma/ast/ExceptionRef.h"
#include "comma/ast/Stmt.h"
#include "comma/ast/TypeRef.h"

#include <algorithm>

using namespace comma;
using llvm::dyn_cast;
using llvm::cast;
using llvm::isa;

Expr *TypeCheck::ensureExpr(Node node)
{
    return ensureExpr(cast_node<Ast>(node));
}

Expr *TypeCheck::ensureExpr(Ast *node)
{
    Expr *expr = dyn_cast<Expr>(node);

    if (expr)
        return expr;

    // Statements and declarations are parsed and typechecked in a controlled
    // manner.  We should never have to cope with either of these things where
    // an expression is expected.
    //
    // The only cases we need to diagnose is when the node denotes a type
    // (by far the most common case), or an exception.
    if (TypeRef *ref = dyn_cast<TypeRef>(node)) {
        report(ref->getLocation(), diag::TYPE_FOUND_EXPECTED_EXPRESSION);
    }
    else {
        ExceptionRef *ref = cast<ExceptionRef>(node);
        report(ref->getLocation(), diag::EXCEPTION_CANNOT_DENOTE_VALUE);
    }
    return 0;
}

IndexedArrayExpr *TypeCheck::acceptIndexedArray(Expr *expr,
                                                SVImpl<Expr*>::Type &indices)
{
    Location loc = expr->getLocation();

    if (!expr->hasResolvedType()) {
        // If the expression is ambiguous we must wait for the top-down pass and
        // the accompanying type context.  Since we will be checking the node
        // entirely at that time simply construct the expression with an
        // unresolved type without checks.
        return new IndexedArrayExpr(expr, &indices[0], indices.size());
    }

    ArrayType *arrTy = dyn_cast<ArrayType>(expr->getType());
    if (!arrTy) {
        report(loc, diag::EXPECTED_ARRAY_FOR_INDEX);
        return 0;
    }

    // Check that the number of indices matches the rank of the array type.
    unsigned numIndices = indices.size();
    if (numIndices != arrTy->getRank()) {
        report(loc, diag::WRONG_NUM_SUBSCRIPTS_FOR_ARRAY);
        return 0;
    }

    // Ensure each index is compatible with the arrays type.  If an index does
    // not check, continue checking each remaining index.
    bool allOK = true;
    for (unsigned i = 0; i < numIndices; ++i) {
        Expr *index = checkExprInContext(indices[i], arrTy->getIndexType(i));
        if (!(indices[i] = index))
            allOK = false;
    }

    if (allOK)
        return new IndexedArrayExpr(expr, &indices[0], numIndices);
    else
        return 0;
}

Expr *TypeCheck::checkExprInContext(Expr *expr, Type *context)
{
    context = resolveType(context);

    // The following sequence dispatches over the types of expressions which are
    // "sensitive" to context, meaning that we might need to patch up the AST so
    // that it conforms to the context.
    if (FunctionCallExpr *fcall = dyn_cast<FunctionCallExpr>(expr))
        return resolveFunctionCall(fcall, context);
    if (IntegerLiteral *intLit = dyn_cast<IntegerLiteral>(expr))
        return resolveIntegerLiteral(intLit, context);
    if (StringLiteral *strLit = dyn_cast<StringLiteral>(expr))
        return resolveStringLiteral(strLit, context);
    if (AggregateExpr *agg = dyn_cast<AggregateExpr>(expr))
        return resolveAggregateExpr(agg, context);
    if (NullExpr *null = dyn_cast<NullExpr>(expr))
        return resolveNullExpr(null, context);
    if (AllocatorExpr *alloc = dyn_cast<AllocatorExpr>(expr))
        return resolveAllocatorExpr(alloc, context);
    if (SelectedExpr *select = dyn_cast<SelectedExpr>(expr))
        return resolveSelectedExpr(select, context);

    assert(expr->hasResolvedType() && "Expression does not have a resolved type!");

    if (covers(resolveType(expr->getType()), context))
        return convertIfNeeded(expr, context);

    // FIXME: Need a better diagnostic here.
    report(expr->getLocation(), diag::INCOMPATIBLE_TYPES);
    return 0;
}

bool TypeCheck::checkExprInContext(Expr *expr, Type::Classification ID)
{
    if (FunctionCallExpr *call = dyn_cast<FunctionCallExpr>(expr)) {
        return resolveFunctionCall(call, ID);
    }
    else if (IntegerLiteral *lit = dyn_cast<IntegerLiteral>(expr)) {
        return resolveIntegerLiteral(lit, ID);
    }
    else if (AggregateExpr *agg = dyn_cast<AggregateExpr>(expr)) {
        // Classification is never a rich enough context to resolve aggregate
        // expressions.
        report(agg->getLocation(), diag::INVALID_CONTEXT_FOR_AGGREGATE);
        return false;
    }

    // Otherwise, the expression must have a resolved type which belongs to the
    // given classification.
    Type *exprTy = resolveType(expr->getType());
    assert(exprTy && "Expression does not have a resolved type!");

    if (!exprTy->memberOf(ID)) {
        // FIXME: Need a better diagnostic here.
        report(expr->getLocation(), diag::INCOMPATIBLE_TYPES);
        return false;
    }
    return true;
}

bool TypeCheck::resolveIntegerLiteral(IntegerLiteral *lit,
                                      Type::Classification ID)
{
    if (lit->isUniversalInteger()) {
        IntegerType *rootTy = resource.getTheRootIntegerType();

        if (!rootTy->memberOf(ID)) {
            report(lit->getLocation(), diag::INCOMPATIBLE_TYPES);
            return false;
        }

        if (!rootTy->baseContains(lit->getValue())) {
            report(lit->getLocation(), diag::VALUE_NOT_IN_RANGE_FOR_TYPE)
                << rootTy->getIdInfo();
            return false;
        }

        lit->setType(rootTy);
        return true;
    }

    // FIXME: Need a better diagnostic here.
    if (!lit->getType()->memberOf(ID)) {
        report(lit->getLocation(), diag::INCOMPATIBLE_TYPES);
        return false;
    }
    return true;
}

Expr *TypeCheck::resolveIntegerLiteral(IntegerLiteral *intLit, Type *context)
{
    if (!intLit->isUniversalInteger()) {
        assert(intLit->getType() == context &&
               "Cannot resolve literal to different type!");
        return intLit;
    }

    IntegerType *subtype = dyn_cast<IntegerType>(context);
    if (!subtype) {
        // FIXME: Need a better diagnostic here.
        report(intLit->getLocation(), diag::INCOMPATIBLE_TYPES);
        return 0;
    }

    // Since integer types are always signed, the literal is within the bounds
    // of the base type iff its width is less than or equal to the base types
    // width.  If the literal is in bounds, sign extend if needed to match the
    // base type.
    llvm::APInt &litValue = intLit->getValue();
    unsigned targetWidth = subtype->getSize();
    unsigned literalWidth = litValue.getBitWidth();
    if (literalWidth < targetWidth)
        litValue.sext(targetWidth);
    else if (literalWidth > targetWidth) {
        report(intLit->getLocation(), diag::VALUE_NOT_IN_RANGE_FOR_TYPE)
            << subtype->getIdInfo();
        return 0;
    }

    DiscreteType::ContainmentResult containment = subtype->contains(litValue);

    // If the value is contained by the context type simply set the type of the
    // literal to the context and return.
    if (containment == DiscreteType::Is_Contained) {
        intLit->setType(context);
        return intLit;
    }

    // If the value is definitely not contained by the context return null.
    if (containment == DiscreteType::Not_Contained) {
        report(intLit->getLocation(), diag::VALUE_NOT_IN_RANGE_FOR_TYPE)
            << subtype->getIdInfo();
        return 0;
    }

    // Otherwise check that the literal is representable as root_integer and, if
    // so, set its type to root_integer and wrap it in a conversion expression.
    //
    // FIXME: It would probably be better to check if the literal fits within
    // the base type of the context.  This would be more efficient for codegen
    // as it would remove unnecessary truncations.
    IntegerType *rootTy = resource.getTheRootIntegerType();
    containment = rootTy->contains(litValue);

    if (containment == DiscreteType::Not_Contained) {
        report(intLit->getLocation(), diag::VALUE_NOT_IN_RANGE_FOR_TYPE)
            << subtype->getIdInfo();
        return 0;
    }

    intLit->setType(rootTy);
    return new ConversionExpr(intLit, context);
}

Expr *TypeCheck::resolveNullExpr(NullExpr *expr, Type *context)
{
    if (expr->hasResolvedType()) {
        assert(covers(expr->getType(), context) &&
               "Cannot resolve to different type");
        return expr;
    }

    // Currently the only constraint which the type context must satisfy is that
    // it represent an access type.
    AccessType *targetType = dyn_cast<AccessType>(context);
    if (!targetType) {
        // FIXME: Need a better diagnostic here.
        report(expr->getLocation(), diag::INCOMPATIBLE_TYPES);
        return 0;
    }

    expr->setType(targetType);
    return expr;
}

Expr *TypeCheck::resolveAllocatorExpr(AllocatorExpr *alloc, Type *context)
{
    if (alloc->hasResolvedType()) {
        assert(covers(alloc->getType(), context) &&
               "Cannot resolve expression to an incompatible type!");
        return alloc;
    }

    // The context must be an access type.
    AccessType *pointerType = dyn_cast<AccessType>(context);
    if (!pointerType) {
        report(alloc->getLocation(), diag::INCOMPATIBLE_TYPES);
        return 0;
    }

    // Check that the pointee type is compatible with the operand of the
    // allocator.
    Type *targetType = pointerType->getTargetType();
    if (alloc->isInitialized()) {
        Expr *operand = alloc->getInitializer();
        if (!(operand = checkExprInContext(operand, targetType)))
            return 0;
        alloc->setInitializer(operand);
    }
    else if (!covers(alloc->getAllocatedType(), targetType)) {
        report(alloc->getLocation(), diag::INCOMPATIBLE_TYPES);
        return 0;
    }

    // Everything looks OK.
    alloc->setType(pointerType);
    return alloc;
}

Expr *TypeCheck::resolveSelectedExpr(SelectedExpr *select, Type *context)
{
    if (select->hasResolvedType()) {
        if (covers(select->getType(), context))
            return convertIfNeeded(select, context);
        report(select->getLocation(), diag::INCOMPATIBLE_TYPES);
        return 0;
    }

    // FIXME: The following is not general enough.  A full implementation is
    // waiting on a reorganization of the type check code that incapsultes the
    // top-down resolution phase into a seperate class.
    Expr *prefix = select->getPrefix();
    IdentifierInfo *selector = select->getSelectorIdInfo();

    FunctionCallExpr *call = dyn_cast<FunctionCallExpr>(prefix);
    assert(prefix && "Cannot resolve this type of selected expression!");

    if (!(prefix = resolveFunctionCall(call, selector, context)))
        return 0;

    // The resolved prefix has a record type.  Locate the required component
    // declaration and resolve this expression.
    RecordType *recTy = cast<RecordType>(prefix->getType());
    RecordDecl *recDecl = recTy->getDefiningDecl();
    ComponentDecl *component = recDecl->getComponent(selector);
    select->resolve(component, component->getType());
    return convertIfNeeded(select, context);
}

Node TypeCheck::acceptInj(Location loc, Node exprNode)
{
    Domoid *domoid = getCurrentDomoid();

    if (!domoid) {
        report(loc, diag::INVALID_INJ_CONTEXT);
        return getInvalidNode();
    }

    // Check that the given expression is of the current domain type.
    DomainType *domTy = domoid->getPercentType();
    Expr *expr = ensureExpr(exprNode);
    if (!expr || !(expr = checkExprInContext(expr, domTy)))
        return getInvalidNode();

    // Check that the carrier type has been defined.
    CarrierDecl *carrier = domoid->getImplementation()->getCarrier();
    if (!carrier) {
        report(loc, diag::CARRIER_TYPE_UNDEFINED);
        return getInvalidNode();
    }

    exprNode.release();
    return getNode(new InjExpr(expr, carrier->getType(), loc));
}

Node TypeCheck::acceptPrj(Location loc, Node exprNode)
{
    Domoid *domoid = getCurrentDomoid();

    if (!domoid) {
        report(loc, diag::INVALID_PRJ_CONTEXT);
        return getInvalidNode();
    }

    // Check that the carrier type has been defined.
    CarrierDecl *carrier = domoid->getImplementation()->getCarrier();
    if (!carrier) {
        report(loc, diag::CARRIER_TYPE_UNDEFINED);
        return getInvalidNode();
    }

    Type *carrierTy = carrier->getType();
    Expr *expr = ensureExpr(exprNode);
    if (!expr || !(expr = checkExprInContext(expr, carrierTy)))
        return getInvalidNode();

    exprNode.release();
    DomainType *prjType = domoid->getPercentType();
    return getNode(new PrjExpr(expr, prjType, loc));
}

Node TypeCheck::acceptIntegerLiteral(llvm::APInt &value, Location loc)
{
    // Integer literals are always unsigned (we have yet to apply a negation
    // operator, for example).   The parser is commited to procuding APInt's
    // which are no wider than necessary to represent the unsigned value.
    // Assert this property of the parser.
    assert((value == 0 || value.countLeadingZeros() == 0) &&
           "Unexpected literal representation!");

    // If non-zero, zero extend the literal by one bit.  Literals internally
    // represented as "minimally sized" signed integers until they are resolved
    // to a particular type.  This convetion simplifies the evaluation of
    // static integer expressions.
    if (value != 0)
        value.zext(value.getBitWidth() + 1);

    return getNode(new IntegerLiteral(value, loc));
}

Node TypeCheck::acceptNullExpr(Location loc)
{
    // We cannot type check null expression until a context has been
    // extablished.  Simply return an expression node and defer checking until
    // the top-down pass.
    return getNode(new NullExpr(loc));
}

Node TypeCheck::acceptAllocatorExpr(Node operandNode, Location loc)
{
    AllocatorExpr *alloc = 0;

    if (QualifiedExpr *operand = lift_node<QualifiedExpr>(operandNode))
        alloc = new AllocatorExpr(operand, loc);
    else {
        TypeDecl *operand = ensureCompleteTypeDecl(operandNode);
        if (!operand)
            return getInvalidNode();
        alloc = new AllocatorExpr(operand->getType(), loc);
    }
    operandNode.release();
    return getNode(alloc);
}

Node TypeCheck::acceptQualifiedExpr(Node qualifierNode, Node exprNode)
{
    // The prefix to a qualified expression must denote a type decl.
    TypeDecl *prefix = ensureCompleteTypeDecl(qualifierNode);
    Expr *expr = ensureExpr(exprNode);

    if (!(prefix && expr))
        return getInvalidNode();

    // Resolve the operand in the type context provided by the prefix.
    if (!(expr = checkExprInContext(expr, prefix->getType())))
        return getInvalidNode();

    // Construct the expression node.
    qualifierNode.release();
    exprNode.release();
    QualifiedExpr *result;
    result = new QualifiedExpr(prefix, expr, getNodeLoc(qualifierNode));
    return getNode(result);
}

Node TypeCheck::acceptDereference(Node prefixNode, Location loc)
{
    Expr *expr = ensureExpr(prefixNode);

    if (!expr)
        return getInvalidNode();

    if (!checkExprInContext(expr, Type::CLASS_Access))
        return getInvalidNode();

    prefixNode.release();
    DereferenceExpr *deref = new DereferenceExpr(expr, loc);
    return getNode(deref);
}




