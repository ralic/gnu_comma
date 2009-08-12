//===-- typecheck/CheckExpr.cpp ------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "IdentifierResolver.h"
#include "comma/ast/Expr.h"
#include "comma/ast/Stmt.h"
#include "comma/typecheck/TypeCheck.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Support/Casting.h"

using namespace comma;
using llvm::dyn_cast;
using llvm::cast;
using llvm::isa;


Node TypeCheck::acceptQualifier(Node typeNode, Location loc)
{
    Type *type = ensureValueType(typeNode, loc);

    if (type) {
        typeNode.release();
        DeclRegion *region = type->getDeclaration()->asDeclRegion();
        assert(region && "Bad type serving as qualifier.");
        return getNode(new Qualifier(region, loc));
    }
    else
        return getInvalidNode();
}

Node TypeCheck::acceptNestedQualifier(Node qualifierNode,
                                      Node typeNode,
                                      Location loc)
{
    Qualifier *qualifier = cast_node<Qualifier>(qualifierNode);
    Type      *type      = ensureValueType(typeNode, loc);

    if (type) {
        typeNode.release();
        DeclRegion *region = type->getDeclaration()->asDeclRegion();
        assert(region && "Bad type serving as qualifier.");
        qualifier->addQualifier(region, loc);
        return qualifierNode;
    }
    else
        return getInvalidNode();
}

// Helper function for acceptDirectName -- called when the identifier in
// question is qualified.
Node TypeCheck::acceptQualifiedName(Node qualNode,
                                    IdentifierInfo *name,
                                    Location loc)
{
    Qualifier  *qualifier = cast_node<Qualifier>(qualNode);
    DeclRegion *region    = qualifier->resolve();
    llvm::SmallVector<FunctionDecl*, 8> decls;

    // Scan the entire set of declaration nodes, matching nullary function decls
    // of the given name.  In addition, look for enumeration declarations which
    // in turn provide a literal of the given name.  This allows the "short hand
    // qualification" of enumeration literals.  For example, given:
    //
    //   domain D with type T is (X); end D;
    //
    // The the qualified name D::X will resolve to D::T::X.  Note however that
    // the two forms are not equivalent, as the former will match all functions
    // named X declared in D (as well as other enumeration literals of the same
    // name).
    for (DeclRegion::DeclIter iter = region->beginDecls();
         iter != region->endDecls(); ++iter) {
        Decl *decl = *iter;
        if (decl->getIdInfo() == name) {
            if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(decl)) {
                if (fdecl->getArity() == 0)
                    decls.push_back(fdecl);
            }
        }
        else if (EnumerationDecl *edecl = dyn_cast<EnumerationDecl>(decl)) {
            // Lift the literals of an enumeration decl.  For example, given:
            //   domain D with type T is (X); end D;
            // Then D::X will match D::T::X.
            if (EnumLiteral *lit = edecl->findLiteral(name))
                decls.push_back(lit);
        }
    }

    if (decls.empty()) {
        report(loc, diag::NAME_NOT_VISIBLE) << name;
        return getInvalidNode();
    }

    // Form the function call node and populate with any additional connectives
    // (to be resolved by the type context of this call).
    FunctionCallExpr *call
        = new FunctionCallExpr(&decls[0], decls.size(), 0, 0, loc);
    call->setQualifier(qualifier);
    qualNode.release();
    return getNode(call);
}

Node TypeCheck::acceptDirectName(IdentifierInfo *name,
                                 Location loc,
                                 Node qualNode)
{
    if (!qualNode.isNull())
        return acceptQualifiedName(qualNode, name, loc);

    IdentifierResolver resolver;

    if (!resolver.resolve(name)) {
        report(loc, diag::NAME_NOT_VISIBLE) << name;
        return getInvalidNode();
    }

    // If there is a direct value, it shadows any other potentialy visible
    // declaration.
    if (resolver.hasDirectValue()) {
        ValueDecl *vdecl = resolver.getDirectValue();
        DeclRefExpr *ref = new DeclRefExpr(vdecl, loc);
        return getNode(ref);
    }

    llvm::SmallVector<Decl *, 8> overloads;
    resolver.filterOverloadsWRTArity(0);
    resolver.filterProcedures();

    // Collect any direct overloads.
    overloads.append(resolver.beginDirectOverloads(),
                     resolver.endDirectOverloads());

    // Continue populating the call with indirect overloads if there are no
    // indirect values visible and return the result.
    if (!resolver.hasIndirectValues()) {
        overloads.append(resolver.beginIndirectOverloads(),
                         resolver.endIndirectOverloads());
        if (!overloads.empty()) {
            llvm::SmallVector<FunctionDecl*, 8> connectives;
            unsigned size = overloads.size();

            for (unsigned i = 0; i < size; ++i)
                connectives.push_back(cast<FunctionDecl>(overloads[i]));

            FunctionCallExpr *call =
                new FunctionCallExpr(&connectives[0], size, 0, 0, loc);
            return getNode(call);
        }
        report(loc, diag::NAME_NOT_VISIBLE) << name;
        return getInvalidNode();
    }

    // Otherwise, there are indirect values.  If we have any direct function
    // decls, return a call expression for them.
    if (!overloads.empty()) {
        llvm::SmallVector<FunctionDecl*, 8> connectives;
        unsigned size = overloads.size();

        for (unsigned i = 0; i < size; ++i)
            connectives.push_back(cast<FunctionDecl>(overloads[i]));

        FunctionCallExpr *call =
            new FunctionCallExpr(&connectives[0], size, 0, 0, loc);
        return getNode(call);
    }

    // If there are any overloadable indirect decls visible, the presence of
    // indirect values hides them.
    if (resolver.hasIndirectOverloads()) {
        report(loc, diag::MULTIPLE_IMPORT_AMBIGUITY);
        return getInvalidNode();
    }

    // If there are multiple indirect values we have an ambiguity.
    if (resolver.numIndirectValues() != 1) {
        report(loc, diag::MULTIPLE_IMPORT_AMBIGUITY);
        return getInvalidNode();
    }

    // Finally, a single indirect value is visible.
    return getNode(new DeclRefExpr(resolver.getIndirectValue(0), loc));
}

Node TypeCheck::acceptFunctionName(IdentifierInfo *name,
                                   Location loc,
                                   Node qualNode)
{
    if (!qualNode.isNull()) {
        Qualifier  *qualifier = cast_node<Qualifier>(qualNode);
        DeclRegion *region    = qualifier->resolve();

        // Collect all of the function declarations in the region with the given
        // name.  If the name does not resolve uniquely, return an
        // OverloadedDeclName, otherwise the decl itself.
        typedef DeclRegion::PredRange PredRange;
        typedef DeclRegion::PredIter  PredIter;
        PredRange range = region->findDecls(name);
        llvm::SmallVector<SubroutineDecl*, 8> decls;

        // Collect all function decls.
        for (PredIter iter = range.first; iter != range.second; ++iter) {
            FunctionDecl *candidate = dyn_cast<FunctionDecl>(*iter);
            if (candidate)
                decls.push_back(candidate);
        }

        if (decls.empty()) {
            report(loc, diag::NAME_NOT_VISIBLE);
            return getInvalidNode();
        }

        if (decls.size() == 1)
            return getNode(decls.front());

        return getNode(new OverloadedDeclName(&decls[0], decls.size()));
    }

    IdentifierResolver resolver;

    if (!resolver.resolve(name) || resolver.hasDirectValue()) {
        report(loc, diag::NAME_NOT_VISIBLE) << name;
        return getInvalidNode();
    }

    llvm::SmallVector<SubroutineDecl *, 8> overloads;
    unsigned numOverloads = 0;
    resolver.filterProcedures();
    resolver.filterNullaryOverloads();

    // Collect any direct overloads.
    {
        typedef IdentifierResolver::DirectOverloadIter iterator;
        iterator E = resolver.endDirectOverloads();
        for (iterator I = resolver.beginDirectOverloads(); I != E; ++I)
            overloads.push_back(cast<FunctionDecl>(*I));
    }

    // Continue populating the call with indirect overloads if there are no
    // indirect values visible and return the result.
    if (!resolver.hasIndirectValues()) {
        typedef IdentifierResolver::IndirectOverloadIter iterator;
        iterator E = resolver.endIndirectOverloads();
        for (iterator I = resolver.beginIndirectOverloads(); I != E; ++I)
            overloads.push_back(cast<FunctionDecl>(*I));
        numOverloads = overloads.size();
        if (numOverloads == 1)
            return getNode(overloads.front());
        else if (numOverloads > 1)
            return getNode(new OverloadedDeclName(&overloads[0], numOverloads));
        else {
            report(loc, diag::NAME_NOT_VISIBLE) << name;
            return getInvalidNode();
        }
    }

    // There are indirect values which shadow all indirect functions.  If we
    // have any direct function decls, return a node for them.
    numOverloads = overloads.size();
    if (numOverloads == 1)
        return getNode(overloads.front());
    else if (numOverloads > 1)
        return getNode(new OverloadedDeclName(&overloads[0], numOverloads));

    // Otherwise, we cannot resolve the name.
    report(loc, diag::NAME_NOT_VISIBLE) << name;
    return getInvalidNode();
}

Node TypeCheck::acceptFunctionCall(Node connective,
                                   Location loc,
                                   NodeVector &argNodes)
{
    std::vector<SubroutineDecl*> decls;
    unsigned targetArity = argNodes.size();

    assert(targetArity > 0 && "Cannot accept nullary function calls!");

    connective.release();

    if (FunctionDecl *fdecl = lift_node<FunctionDecl>(connective)) {
        if (fdecl->getArity() == targetArity)
            decls.push_back(fdecl);
        else {
            report(loc, diag::WRONG_NUM_ARGS_FOR_SUBROUTINE)
                << fdecl->getIdInfo();
            return getInvalidNode();
        }
    }
    else {
        OverloadedDeclName *odn = cast_node<OverloadedDeclName>(connective);
        for (OverloadedDeclName::iterator iter = odn->begin();
             iter != odn->end(); ++iter) {
            FunctionDecl *fdecl = cast<FunctionDecl>(*iter);
            if (fdecl->getArity() == targetArity)
                decls.push_back(fdecl);
        }

        delete odn;

        // FIXME: Report that there are no functions with the required arity
        // visible.
        if (decls.empty()) {
            report(loc, diag::NAME_NOT_VISIBLE) << odn->getIdInfo();
            return getInvalidNode();
        }
    }

    return acceptSubroutineCall(decls, loc, argNodes);
}

Node TypeCheck::acceptSubroutineCall(std::vector<SubroutineDecl*> &decls,
                                     Location loc,
                                     NodeVector &argNodes)
{
    llvm::SmallVector<Expr*, 8> args;
    unsigned numArgs = argNodes.size();
    IdentifierInfo *name = decls[0]->getIdInfo();

    // Convert the argument nodes to Expr's.
    for (unsigned i = 0; i < numArgs; ++i)
        args.push_back(cast_node<Expr>(argNodes[i]));

    if (decls.size() == 1) {
        Node call = checkSubroutineCall(decls[0], loc, &args[0], numArgs);
        if (call.isValid()) argNodes.release();
        return call;
    }

    // We use the following bit vector to indicate which elements of the decl
    // vector are applicable as we resolve the subroutine call with respect to
    // the given arguments.
    llvm::BitVector declFilter(decls.size(), true);

    // First, reduce the set of declarations to include only those which can
    // accept any keyword selectors provided.
    for (unsigned i = 0; i < decls.size(); ++i) {
        SubroutineDecl *decl = decls[i];
        for (unsigned j = 0; j < numArgs; ++j) {
            Expr *arg = args[j];
            if (KeywordSelector *selector = dyn_cast<KeywordSelector>(arg)) {
                IdentifierInfo *key      = selector->getKeyword();
                Location        keyLoc   = selector->getLocation();
                int             keyIndex = decl->getKeywordIndex(key);
                if (keyIndex < 0) {
                    declFilter[i] = false;
                    if (declFilter.none()) {
                        report(keyLoc, diag::SUBROUTINE_HAS_NO_SUCH_KEYWORD)
                            << key << name;
                        return getInvalidNode();
                    }
                    break;
                }
            }
        }
    }

    // Reduce the set of declarations with respect to the types of its
    // arguments.
    for (unsigned i = 0; i < decls.size(); ++i) {
        SubroutineDecl *decl = decls[i];
        for (unsigned j = 0; j < numArgs && declFilter[i]; ++j) {
            Expr     *arg = args[j];
            unsigned  targetIndex = j;

            if (KeywordSelector *selector = dyn_cast<KeywordSelector>(args[j])) {
                arg = selector->getExpression();
                targetIndex = decl->getKeywordIndex(selector->getKeyword());
            }

            Type *targetType = decl->getArgType(targetIndex);

            // If the argument as a fully resolved type, all we currently do is
            // test for type equality.
            if (arg->hasType()) {
                if (!targetType->equals(arg->getType())) {
                    declFilter[i] = false;
                    // If the set of applicable declarations has been reduced to
                    // zero, report this argument as ambiguous.
                    if (declFilter.none())
                        report(arg->getLocation(), diag::AMBIGUOUS_EXPRESSION);
                }
                continue;
            }

            // Otherwise, we have an unresolved argument expression (which must
            // be a function call expression).
            typedef FunctionCallExpr::ConnectiveIterator ConnectiveIter;
            FunctionCallExpr *argCall = cast<FunctionCallExpr>(arg);
            bool applicableArgument = false;

            // Check if at least one interpretation of the argument satisfies
            // the current target type.
            for (ConnectiveIter iter = argCall->beginConnectives();
                 iter != argCall->endConnectives(); ++iter) {
                FunctionDecl *connective = cast<FunctionDecl>(*iter);
                Type *returnType = connective->getReturnType();
                if (targetType->equals(returnType)) {
                    applicableArgument = true;
                    break;
                }
            }

            // If this argument is not applicable (meaning, there is no
            // interpretation of the argument for this particular decl), filter
            // out the decl.  Also, if we have exhausted all possibilities, use
            // the current argument as context for generating a diagnostic.
            if (!applicableArgument) {
                declFilter[i] = false;
                if (declFilter.none())
                    report(arg->getLocation(), diag::AMBIGUOUS_EXPRESSION);
            }
        }
    }

    // If all of the declarations have been filtered out, it is due to ambiguous
    // arguments.  Simply return (we have already generated a diagnostic).
    if (declFilter.none())
        return getInvalidNode();

    // If we have a unique declaration, check the matching call.
    if (declFilter.count() == 1) {
        argNodes.release();
        SubroutineDecl *decl = decls[declFilter.find_first()];
        Node result = checkSubroutineCall(decl, loc, &args[0], numArgs);
        if (result.isValid()) argNodes.release();
        return result;
    }

    // If we are dealing with functions, the resolution of the call will depend
    // on the resolution of the return type.  If we are dealing with procedures,
    // then the call is ambiguous.
    if (isa<FunctionDecl>(decls[0])) {
        llvm::SmallVector<FunctionDecl*, 4> connectives;
        for (unsigned i = 0; i < decls.size(); ++i)
            if (declFilter[i])
                connectives.push_back(cast<FunctionDecl>(decls[i]));
        FunctionCallExpr *call =
            new FunctionCallExpr(&connectives[0], connectives.size(),
                                 &args[0], numArgs, loc);
        argNodes.release();
        return getNode(call);
    }
    else {
        report(loc, diag::AMBIGUOUS_EXPRESSION);
        return getInvalidNode();
    }
}

Node TypeCheck::checkSubroutineCall(SubroutineDecl  *decl,
                                    Location loc,
                                    Expr **args,
                                    unsigned numArgs)

{
    if (decl->getArity() != numArgs) {
        report(loc, diag::WRONG_NUM_ARGS_FOR_SUBROUTINE) << decl->getIdInfo();
        return getInvalidNode();
    }

    llvm::SmallVector<Expr*, 4> sortedArgs(numArgs);
    unsigned numPositional = 0;

    // Sort the arguments wrt the functions keyword profile.
    for (unsigned i = 0; i < numArgs; ++i) {
        Expr *arg = args[i];

        if (KeywordSelector *selector = dyn_cast<KeywordSelector>(arg)) {
            IdentifierInfo  *key      = selector->getKeyword();
            Location         keyLoc   = selector->getLocation();
            int              keyIdx   = decl->getKeywordIndex(key);

            // Ensure the given keyword exists.
            if (keyIdx < 0) {
                report(keyLoc, diag::SUBROUTINE_HAS_NO_SUCH_KEYWORD)
                    << key << decl->getIdInfo();
                return getInvalidNode();
            }

            // The corresponding index of the keyword must be greater than the
            // number of supplied positional parameters (otherwise it would
            // `overlap' a positional parameter).
            if ((unsigned)keyIdx < numPositional) {
                report(keyLoc, diag::PARAM_PROVIDED_POSITIONALLY) << key;
                return getInvalidNode();
            }

            // Ensure that this keyword is not a duplicate of any preceding
            // keyword.
            for (unsigned j = numPositional; j < i; ++j) {
                KeywordSelector *prevSelector;
                prevSelector = cast<KeywordSelector>(args[j]);

                if (prevSelector->getKeyword() == key) {
                    report(keyLoc, diag::DUPLICATE_KEYWORD) << key;
                    return getInvalidNode();
                }
            }

            // Add the argument in its proper position.
            sortedArgs[keyIdx] = arg;
        }
        else {
            numPositional++;
            sortedArgs[i] = arg;
        }
    }

    if (!checkSubroutineArguments(decl, &sortedArgs[0], numArgs))
        return getInvalidNode();

    if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(decl)) {
        FunctionCallExpr *call =
            new FunctionCallExpr(fdecl, &sortedArgs[0], numArgs, loc);
        return getNode(call);
    }
    else {
        ProcedureDecl *pdecl = cast<ProcedureDecl>(decl);
        ProcedureCallStmt *call =
            new ProcedureCallStmt(pdecl, &sortedArgs[0], numArgs, loc);
        return getNode(call);
    }
}

/// Checks that the supplied array of arguments are compatible with those of
/// the given decl.  This is a helper method for checkSubroutineCall.
///
/// It is assumed that the number of arguments passed matches the number
/// expected by the decl.  This function checks that the argument types and
/// modes are compatible with that of the given decl.  Returns true if the check
/// succeeds, false otherwise and appropriate diagnostics are posted.
bool TypeCheck::checkSubroutineArguments(SubroutineDecl *decl,
                                         Expr **args,
                                         unsigned numArgs)
{
    // Check each argument types wrt this decl.
    for (unsigned i = 0; i < numArgs; ++i) {
        Type *targetType = decl->getArgType(i);
        Expr *arg = args[i];
        Location argLoc = arg->getLocation();
        PM::ParameterMode targetMode = decl->getParamMode(i);

        if (KeywordSelector *selector = dyn_cast<KeywordSelector>(arg))
            arg = selector->getExpression();

        if (!checkType(arg, targetType))
            return false;

        // If the target mode is either "out" or "in out", ensure that the
        // argument provided is compatable.
        if (targetMode == PM::MODE_OUT or targetMode == PM::MODE_IN_OUT) {
            if (DeclRefExpr *declRef = dyn_cast<DeclRefExpr>(arg)) {
                ValueDecl *vdecl = declRef->getDeclaration();
                if (ParamValueDecl *param = dyn_cast<ParamValueDecl>(vdecl)) {
                    // If the argument is of mode IN, then so too must be the
                    // target mode.
                    if (param->getParameterMode() == PM::MODE_IN) {
                        report(argLoc, diag::IN_PARAMETER_NOT_MODE_COMPATABLE)
                            << param->getString() << targetMode;
                        return false;
                    }
                }
                else {
                    // The only other case (currently) are ObjectDecls, which
                    // are always usable.
                    assert(isa<ObjectDecl>(vdecl) && "Cannot typecheck decl!");
                }
                continue;
            }

            // The argument is not usable in an "out" or "in out" context.
            report(argLoc, diag::EXPRESSION_NOT_MODE_COMPATABLE) << targetMode;
            return false;
        }
    }
    return true;
}

// Resolves the given call expression (which must be nullary function call,
// i.e. one without arguments) to one which satisfies the given target type and
// returns true.  Otherwise, false is returned and the appropriate diagnostics
// are emitted.
bool TypeCheck::resolveNullaryFunctionCall(FunctionCallExpr *call,
                                           Type *targetType)
{
    assert(call->getNumArgs() == 0 &&
           "Call expression has too many arguments!");

    typedef FunctionCallExpr::ConnectiveIterator ConnectiveIter;
    ConnectiveIter iter    = call->beginConnectives();
    ConnectiveIter endIter = call->endConnectives();
    FunctionDecl *connective = 0;

    for ( ; iter != endIter; ++iter) {
        FunctionDecl *candidate = *iter;
        Type *returnType = candidate->getReturnType();

        if (targetType->equals(returnType)) {
            if (connective) {
                report(call->getLocation(), diag::AMBIGUOUS_EXPRESSION);
                return false;
            }
            else
                connective = candidate;
        }
    }
    call->resolveConnective(connective);
    return true;
}

// Resolves the given call expression (which should have multiple candidate
// connectives) to one which satisfies the given target type and returns true.
// Otherwise, false is returned and the appropriate diagnostics are emitted.
bool TypeCheck::resolveFunctionCall(FunctionCallExpr *call, Type *targetType)
{
    if (call->getNumArgs() == 0)
        return resolveNullaryFunctionCall(call, targetType);

    typedef FunctionCallExpr::ConnectiveIterator ConnectiveIter;
    ConnectiveIter iter    = call->beginConnectives();
    ConnectiveIter endIter = call->endConnectives();
    FunctionDecl  *fdecl   = 0;

    for ( ; iter != endIter; ++iter) {
        FunctionDecl *candidate  = cast<FunctionDecl>(*iter);
        Type         *returnType = candidate->getReturnType();
        if (targetType->equals(returnType)) {
            if (fdecl) {
                report(call->getLocation(), diag::AMBIGUOUS_EXPRESSION);
                return false;
            }
            else
                fdecl = candidate;
        }
    }

    // FIXME:  Diagnose that there are no functions of the given name satisfying
    // the target type.
    if (!fdecl) {
        report(call->getLocation(), diag::AMBIGUOUS_EXPRESSION);
        return false;
    }

    // Traverse the argument set, patching up any unresolved argument
    // expressions.  We also need to sort the arguments according to the keyword
    // selections, since the connectives do not necessarily respect a uniform
    // ordering.
    bool     status  = true;
    unsigned numArgs = call->getNumArgs();
    llvm::SmallVector<Expr*, 8> sortedArgs(numArgs);

    for (unsigned i = 0; i < call->getNumArgs(); ++i) {
        FunctionCallExpr *argCall;
        unsigned          argIndex = i;
        Expr             *arg      = call->getArg(i);

        // If we have a keyword selection, locate the corresponding index, and
        // resolve the selection to its corresponding expression.
        if (KeywordSelector *select = dyn_cast<KeywordSelector>(arg)) {
            arg      = select->getExpression();
            argIndex = fdecl->getKeywordIndex(select->getKeyword());
            sortedArgs[argIndex] = select;
        }
        else
            sortedArgs[argIndex] = arg;

        argCall = dyn_cast<FunctionCallExpr>(arg);
        if (argCall && argCall->isAmbiguous())
            status = status &&
                resolveFunctionCall(argCall, fdecl->getArgType(argIndex));
    }
    call->resolveConnective(fdecl);
    return status;
}

Node TypeCheck::acceptInj(Location loc, Node exprNode)
{
    Expr   *expr   = cast_node<Expr>(exprNode);
    Domoid *domoid = getCurrentDomoid();

    if (!domoid) {
        report(loc, diag::INVALID_INJ_CONTEXT);
        return getInvalidNode();
    }

    // Check that the given expression is of the current domain type.
    DomainType *domTy = domoid->getPercent();
    Type      *exprTy = expr->getType();
    if (!domTy->equals(exprTy)) {
        report(loc, diag::INCOMPATIBLE_TYPES);
        return getInvalidNode();
    }

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
    Expr   *expr   = cast_node<Expr>(exprNode);
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

    // Check that the given expression is of the carrier type.
    Type *carrierTy = carrier->getType();
    Type *exprTy    = expr->getType();
    if (!carrierTy->equals(exprTy)) {
        report(loc, diag::INCOMPATIBLE_TYPES);
        return getInvalidNode();
    }

    exprNode.release();
    return getNode(new PrjExpr(expr, domoid->getPercent(), loc));
}
