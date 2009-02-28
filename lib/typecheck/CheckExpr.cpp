//===-- typecheck/CheckExpr.cpp ------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

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
    Type *domain = ensureDomainType(typeNode, loc);

    if (domain)
        return Node(new Qualifier(domain, loc));
    else
        return Node::getInvalidNode();
}

// FIXME: Implement.
Node TypeCheck::acceptNestedQualifier(Node     qualifierNode,
                                      Node     typeNode,
                                      Location loc)
{
    assert(false && "Nested qualifiers not yet implemented!");
    return Node::getInvalidNode();
}

// This function is a helper to acceptDirectName.  It checks that an arbitrary
// decl denotes a direct name (a value decl or nullary function) and emits
// diagnostics otherwise.  Returns true when the decl is accepted and populates
// the given node with an expression representing the name.  Otherwise false is
// returned and node is not touched.
bool TypeCheck::resolveDirectDecl(Decl           *candidate,
                                  IdentifierInfo *name,
                                  Location        loc,
                                  Node           &node)
{
    if (isa<ValueDecl>(candidate)) {
        ValueDecl  *decl = cast<ValueDecl>(candidate);
        DeclRefExpr *ref = new DeclRefExpr(decl, loc);
        node = Node(ref);
        return true;
    }

    if (isa<FunctionDecl>(candidate)) {
        FunctionDecl *decl = cast<FunctionDecl>(candidate);

        if (decl->getArity() != 0) {
            report(loc, diag::WRONG_NUM_ARGS_FOR_SUBROUTINE) << name;
            node = Node::getInvalidNode();
        }
        else {
            FunctionCallExpr *call = new FunctionCallExpr(decl, 0, 0, loc);
            node = Node(call);
        }
        return true;
    }

    if (isa<ProcedureDecl>(candidate)) {
        report(loc, diag::PROCEDURE_IN_EXPRESSION);
        node = Node::getInvalidNode();
        return true;
    }

    return false;
}

// FIXME: This function is just an example of where name lookup is going.  The
// logic herein needs to be factored out appropriately.
Node TypeCheck::acceptDirectName(IdentifierInfo *name, Location loc)
{
    Homonym *homonym = name->getMetadata<Homonym>();

    if (!homonym) {
        report(loc, diag::NAME_NOT_VISIBLE) << name;
        return Node::getInvalidNode();
    }

    // Singleton case.
    if (homonym->isSingleton()) {
        Node node = Node::getInvalidNode();
        if (resolveDirectDecl(homonym->asDeclaration(), name, loc, node))
            return node;
        report(loc, diag::NAME_NOT_VISIBLE) << name;
        return Node::getInvalidNode();
    }

    if (homonym->isEmpty()) {
        report(loc, diag::NAME_NOT_VISIBLE) << name;
        return Node::getInvalidNode();
    }

    // The homonym is loaded (multiple bindings are in effect for this name).
    // First examine the direct declarations for a value of the given name.
    for (Homonym::DirectIterator iter = homonym->beginDirectDecls();
         iter != homonym->endDirectDecls(); ++iter) {
        Node node = Node::getInvalidNode();
        if (resolveDirectDecl(*iter, name, loc, node))
            return node;
    }

    // Otherwise, scan the full set of imported declarations, and partition the
    // import decls into two sets:  one containing all value declarations, the
    // other containing all nullary function declatations.
    llvm::SmallVector<FunctionDecl*, 4> functionDecls;
    llvm::SmallVector<ValueDecl*, 4>    valueDecls;

    for (Homonym::ImportIterator iter = homonym->beginImportDecls();
         iter != homonym->endImportDecls(); ++iter) {
        Decl *candidate = *iter;
        if (ValueDecl *decl = dyn_cast<ValueDecl>(candidate))
            valueDecls.push_back(decl);
        else if (FunctionDecl *decl = dyn_cast<FunctionDecl>(candidate))
            if (decl->getArity() == 0) functionDecls.push_back(decl);
    }

    // If both partitions are empty, then no name is visible.
    if (valueDecls.empty() && functionDecls.empty()) {
        report(loc, diag::NAME_NOT_VISIBLE) << name;
        return Node::getInvalidNode();
    }

    if (valueDecls.empty()) {
        // Possibly several nullary functions are visible.  We cannot resolve
        // further so build a function call with an overloaded set of
        // connectives.
        //
        // FIXME: We could check that the set collected admits at least two
        // distinct return types.
        FunctionCallExpr *call =
            new FunctionCallExpr(functionDecls[0], 0, 0, loc);
        for (unsigned i = 1; i < functionDecls.size(); ++i)
            call->addConnective(functionDecls[i]);
        return Node(call);
    }
    else if (functionDecls.empty()) {
        // If a there is more than one value decl in effect, then we have an
        // ambiguity.  Value decls are not overloadable.
        if (valueDecls.size() > 1) {
            report(loc, diag::AMBIGUOUS_EXPRESSION);
            return Node::getInvalidNode();
        }
        // Otherwise, we can fully resolve the value.
        DeclRefExpr *ref = new DeclRefExpr(valueDecls[0], loc);
        return Node(ref);
    }
    else {
        // There are both values and nullary function declarations in scope.
        // Since values are not overloadable entities we have a conflict and
        // this expression requires qualification.
        report(loc, diag::MULTIPLE_IMPORT_AMBIGUITY);
        return Node::getInvalidNode();
    }
}

Node TypeCheck::acceptQualifiedName(Node            qualNode,
                                    IdentifierInfo *name,
                                    Location        loc)
{
    Qualifier         *qualifier = cast_node<Qualifier>(qualNode);
    DeclarativeRegion *region    = qualifier->resolve();

    // Lookup the name in the resolved declarative region.
    typedef DeclarativeRegion::DeclRange DeclRange;
    typedef DeclarativeRegion::DeclIter  DeclIter;

    DeclRange range = region->findDecls(name);

    if (range.first == range.second) {
        report(loc, diag::NAME_NOT_VISIBLE) << name;
        return Node::getInvalidNode();
    }

    // Currently, the only component of a domain which we support are function
    // decls.  Collect the nullary functions.
    llvm::SmallVector<FunctionDecl*, 4> functionDecls;

    for (DeclIter iter = range.first; iter != range.second; ++iter) {
        if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(iter->second)) {
            if (fdecl->getArity() == 0)
                functionDecls.push_back(fdecl);
        }
    }

    // FIXME: Report that there are no nullary functions declared in this
    // domain.
    if (functionDecls.empty()) {
        report(loc, diag::NAME_NOT_VISIBLE);
        return Node::getInvalidNode();
    }

    // Form the function call node.
    FunctionCallExpr *call = new FunctionCallExpr(functionDecls[0], 0, 0, loc);
    for (unsigned i = 1; i < functionDecls.size(); ++i)
        call->addConnective(functionDecls[i]);
    call->setQualifier(qualifier);
    return Node(call);
}

Node TypeCheck::acceptFunctionCall(IdentifierInfo *name,
                                   Location        loc,
                                   Node           *argNodes,
                                   unsigned        numArgs)
{
    return acceptSubroutineCall(name, loc, argNodes, numArgs, true);
}


// Note that this function will evolve to take a Node in place of an identifier,
// and will be renamed to acceptFunctionCall.
Node TypeCheck::acceptSubroutineCall(IdentifierInfo *name,
                                     Location        loc,
                                     Node           *argNodes,
                                     unsigned        numArgs,
                                     bool            checkFunction)
{
    llvm::SmallVector<Expr*, 8> args;

    // Convert the argument nodes to Expr's.
    for (unsigned i = 0; i < numArgs; ++i)
        args.push_back(cast_node<Expr>(argNodes[i]));

    llvm::SmallVector<SubroutineDecl*, 8> routineDecls;
    Homonym *homonym = name->getMetadata<Homonym>();

    lookupSubroutineDecls(homonym, numArgs, routineDecls, checkFunction);

    if (routineDecls.empty()) {
        report(loc, diag::NAME_NOT_VISIBLE) << name;
        return Node::getInvalidNode();
    }

    if (routineDecls.size() == 1)
        return checkSubroutineCall(routineDecls[0], loc, &args[0], numArgs);

    // We use the following bit vector to indicate which elements of the
    // routineDecl vector are applicable as we resolve the subroutine call with
    // respect to the given arguments.
    llvm::BitVector declFilter(routineDecls.size(), true);

    // First, reduce the set of declarations to include only those which can
    // accept any keyword selectors provided.
    for (unsigned i = 0; i < routineDecls.size(); ++i) {
        SubroutineDecl *decl = routineDecls[i];
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
                        return Node::getInvalidNode();
                    }
                    break;
                }
            }
        }
    }

    // Reduce the set of declarations with respect to the types of its
    // arguments.
    for (unsigned i = 0; i < routineDecls.size(); ++i) {
        SubroutineDecl *decl = routineDecls[i];
        for (unsigned j = 0; j < numArgs && declFilter[i]; ++j) {
            Expr     *arg         = args[j];
            unsigned  targetIndex = j;

            if (KeywordSelector *selector = dyn_cast<KeywordSelector>(args[j])) {
                arg         = selector->getExpression();
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
            bool   applicableArgument = false;

            // Check if at least one interpretation of the argument satisfies
            // the current target type.
            for (ConnectiveIter iter = argCall->beginConnectives();
                 iter != argCall->endConnectives(); ++iter) {
                FunctionDecl *connective = *iter;
                Type         *returnType = connective->getReturnType();
                if (targetType->equals(returnType)) {
                    applicableArgument = true;
                    break;
                }
            }

            // If this argument is not applicable (meaning, there is no
            // interpretation of the argument for this particular decl), filter
            // out the decl.
            if (!applicableArgument) {
                declFilter[i] = false;
                if (declFilter.none())
                    report(arg->getLocation(), diag::AMBIGUOUS_EXPRESSION);
            }
        }
    }

    // If all of the declarations have been filtered out, it is due to ambiguous
    // arguments.  Simply return.
    if (declFilter.none())
        return Node::getInvalidNode();

    // If we have a unique declaration, check the matching call.
    if (declFilter.count() == 1) {
        SubroutineDecl *decl = routineDecls[declFilter.find_first()];
        return checkSubroutineCall(decl, loc, &args[0], numArgs);
    }

    // If we are dealing with functions, the resolution of the call will depend
    // on the resolution of the return type.  If we are dealing with prcedures,
    // then the call is ambiguous.
    if (checkFunction) {
        llvm::SmallVector<FunctionDecl*, 4> connectives;
        for (unsigned i = 0; i < routineDecls.size(); ++i)
            if (declFilter[i]) {
                FunctionDecl *fdecl = cast<FunctionDecl>(routineDecls[i]);
                connectives.push_back(fdecl);
            }
        FunctionCallExpr *call =
            new FunctionCallExpr(connectives[0], &args[0], numArgs, loc);
        for (unsigned i = 1; i < connectives.size(); ++i)
            call->addConnective(connectives[i]);
        return Node(call);
    }
    else {
        report(loc, diag::AMBIGUOUS_EXPRESSION);
        return Node::getInvalidNode();
    }
}

// This function looks up the set of visible subroutines of a certain arity in
// the given homonym and poulates the vector routineDecls with the results.  If
// lookupFunctions is true, this method scans for functions, otherwise for
// procedures.  Note that this function is not valid for lookups of arity 0.
void TypeCheck::lookupSubroutineDecls(
    Homonym *homonym,
    unsigned arity,
    llvm::SmallVector<SubroutineDecl*, 8> &routineDecls,
    bool lookupFunctions)
{
    SubroutineDecl *decl;       // Declarations provided by the homonym.
    SubroutineType *type;       // Type of `decl'.
    SubroutineType *shadowType; // Type of previous direct lookups.

    assert (arity != 0 &&
            "This method should not be used to look up nullary subroutines!");

    if (homonym->isEmpty())
        return;

    if (homonym->isSingleton()) {
        if (lookupFunctions)
            decl = dyn_cast<FunctionDecl>(homonym->asDeclaration());
        else
            decl = dyn_cast<ProcedureDecl>(homonym->asDeclaration());
        if (decl && decl->getArity() == arity)
            routineDecls.push_back(decl);
        return;
    }

    // Accumulate any direct declarations.
    for (Homonym::DirectIterator iter = homonym->beginDirectDecls();
         iter != homonym->endDirectDecls(); ++iter) {
        if (lookupFunctions)
            decl = dyn_cast<FunctionDecl>(*iter);
        else
            decl = dyn_cast<ProcedureDecl>(*iter);
        if (decl && decl->getArity() == arity) {
            type = decl->getType();
            for (unsigned i = 0; i < routineDecls.size(); ++i) {
                shadowType = routineDecls[i]->getType();
                if (shadowType->equals(type)) {
                    type = 0;
                    break;
                }
            }
            if (type)
                routineDecls.push_back(decl);
        }
    }

    // Accumulate the any import declarations, ensuring that any directly
    // visible declarations shadow those imports with matching types.  Imported
    // declarations do not shadow eachother.
    unsigned numDirectDecls = routineDecls.size();
    for (Homonym::ImportIterator iter = homonym->beginImportDecls();
         iter != homonym->endImportDecls(); ++iter) {
        if (lookupFunctions)
            decl = dyn_cast<FunctionDecl>(*iter);
        else
            decl = dyn_cast<ProcedureDecl>(*iter);
        if (decl && decl->getArity() == arity) {
            type = decl->getType();
            for (unsigned i = 0; i < numDirectDecls; ++i) {
                shadowType = routineDecls[i]->getType();
                if (shadowType->equals(type)) {
                    type = 0;
                    break;
                }
            }
            if (type)
                routineDecls.push_back(decl);
        }
    }
}

Node TypeCheck::checkSubroutineCall(SubroutineDecl  *decl,
                                    Location         loc,
                                    Expr           **args,
                                    unsigned         numArgs)

{
    if (decl->getArity() != numArgs) {
        report(loc, diag::WRONG_NUM_ARGS_FOR_SUBROUTINE) << decl->getIdInfo();
        return Node::getInvalidNode();
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
                return Node::getInvalidNode();
            }

            // The corresponding index of the keyword must be greater than the
            // number of supplied positional parameters (otherwise it would
            // `overlap' a positional parameter).
            if ((unsigned)keyIdx < numPositional) {
                report(keyLoc, diag::PARAM_PROVIDED_POSITIONALLY) << key;
                return Node::getInvalidNode();
            }

            // Ensure that this keyword is not a duplicate of any preceeding
            // keyword.
            for (unsigned j = numPositional; j < i; ++j) {
                KeywordSelector *prevSelector;
                prevSelector = cast<KeywordSelector>(args[j]);

                if (prevSelector->getKeyword() == key) {
                    report(keyLoc, diag::DUPLICATE_KEYWORD) << key;
                    return Node::getInvalidNode();
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

    // Check each argument types wrt this decl.
    for (unsigned i = 0; i < numArgs; ++i) {
        Type *targetType = decl->getArgType(i);
        Expr *arg        = sortedArgs[i];
        Type *argType;

        if (KeywordSelector *selector = dyn_cast<KeywordSelector>(arg))
            arg = selector->getExpression();

        if (arg->hasType()) {
            argType = arg->getType();
            if (!targetType->equals(argType)) {
                report(arg->getLocation(), diag::INCOMPATABLE_TYPES);
                return Node::getInvalidNode();
            }
        }
        else {
            FunctionCallExpr *callExpr = cast<FunctionCallExpr>(arg);
            if (!resolveFunctionCall(callExpr, targetType))
                return Node::getInvalidNode();
        }
    }

    if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(decl)) {
        FunctionCallExpr *call =
            new FunctionCallExpr(fdecl, &sortedArgs[0], numArgs, loc);
        return Node(call);
    }
    else {
        ProcedureDecl *pdecl = cast<ProcedureDecl>(decl);
        ProcedureCallStmt *call =
            new ProcedureCallStmt(pdecl, &sortedArgs[0], numArgs, loc);
        return Node(call);
    }
}

// Resolves the given call expression (which should have multiple candidate
// connectives) to one which satisfies the given target type and returns true.
// Otherwise, false is returned and the appropriated diagnostics are emitted.
bool TypeCheck::resolveFunctionCall(FunctionCallExpr *call, Type *targetType)
{
    typedef FunctionCallExpr::ConnectiveIterator ConnectiveIter;
    ConnectiveIter iter    = call->beginConnectives();
    ConnectiveIter endIter = call->endConnectives();
    FunctionDecl  *fdecl   = 0;

    for ( ; iter != endIter; ++iter) {
        FunctionDecl *candidate  = *iter;
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
    call->setConnective(fdecl);
    return status;
}
