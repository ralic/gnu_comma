//===-- typecheck/TypeCheck.h --------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008-2010, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#ifndef COMMA_TYPECHECK_TYPECHECK_HDR_GUARD
#define COMMA_TYPECHECK_TYPECHECK_HDR_GUARD


#include "Scope.h"
#include "Stencil.h"
#include "comma/ast/AstBase.h"
#include "comma/ast/AstResource.h"
#include "comma/ast/Cunit.h"
#include "comma/ast/Type.h"
#include "comma/basic/Diagnostic.h"
#include "comma/basic/TextManager.h"
#include "comma/typecheck/Checker.h"

#include "llvm/Support/Casting.h"

#include <stack>

namespace llvm {

class APInt;

} // end llvm namespace.

namespace comma {

class TypeCheck : public Checker {

public:
    TypeCheck(TextManager &manager, Diagnostic &diag,
              AstResource &resource, CompilationUnit *cunit);

    ~TypeCheck();

    /// \name ParseClient Requirements.
    ///
    /// \brief Declaration of the ParseClient interface.
    ///
    /// \see ParseClient.
    //@{
    void acceptWithClause(Location loc, IdentifierInfo **names,
                          unsigned numNames);

    bool beginPackageSpec(IdentifierInfo *name, Location loc);
    void beginPackagePrivatePart(Location loc);
    void endPackageSpec();
    bool beginPackageBody(IdentifierInfo *name, Location loc);
    void endPackageBody();

    void beginFunctionDeclaration(IdentifierInfo *name, Location loc);
    void beginProcedureDeclaration(IdentifierInfo *name, Location loc);


    void acceptSubroutineParameter(IdentifierInfo *formal, Location loc,
                                   Node typeNode, PM::ParameterMode mode);

    void acceptFunctionReturnType(Node typeNode);

    Node endSubroutineDeclaration(bool definitionFollows);

    Node beginSubroutineDefinition(Node declarationNode);
    void endSubroutineBody(Node contextNode);
    void endSubroutineDefinition();

    Node acceptDirectName(IdentifierInfo *name, Location loc,
                          bool forStatement);

    Node acceptCharacterLiteral(IdentifierInfo *lit, Location loc);

    Node acceptSelectedComponent(Node prefix,
                                 IdentifierInfo *name,
                                 Location loc,
                                 bool forStatement);

    Node acceptParameterAssociation(IdentifierInfo *key,
                                    Location loc, Node rhs);

    Node acceptApplication(Node prefix, NodeVector &argNodes);

    Node acceptAttribute(Node prefix, IdentifierInfo *name, Location loc);

    Node finishName(Node name);

    void beginAggregate(Location loc);
    void acceptPositionalAggregateComponent(Node component);
    Node acceptAggregateKey(Node lower, Node upper);
    Node acceptAggregateKey(IdentifierInfo *name, Location loc);
    Node acceptAggregateKey(Node key);
    void acceptKeyedAggregateComponent(NodeVector &keys,
                                       Node expr, Location loc);
    void acceptAggregateOthers(Location loc, Node component);
    Node endAggregate();

    Node beginWhileStmt(Location loc, Node condition,
                        IdentifierInfo *tag, Location tagLoc);
    Node endWhileStmt(Node whileNode);

    Node beginLoopStmt(Location loc, IdentifierInfo *tag, Location tagLoc);
    Node endLoopStmt(Node loopNode);

    Node beginForStmt(Location loc, IdentifierInfo *iterName, Location iterLoc,
                      Node control, bool isReversed,
                      IdentifierInfo *tag, Location tagLoc);
    Node endForStmt(Node forNode);

    Node acceptDSTDefinition(Node name, Node lower, Node upper);
    Node acceptDSTDefinition(Node nameOrAttribute, bool isUnconstrained);
    Node acceptDSTDefinition(Node lower, Node upper);

    Node acceptSubtypeIndication(Node prefix);
    Node acceptSubtypeIndication(Node prefix, Node lower, Node upper);
    Node acceptSubtypeIndication(Node prefix, NodeVector &arguments);

    Node acceptExitStmt(Location exitLoc,
                        IdentifierInfo *tag, Location tagLoc,
                        Node condition);

    bool acceptObjectDeclaration(Location loc, IdentifierInfo *name,
                                 Node type, Node initializer);

    bool acceptRenamedObjectDeclaration(Location loc, IdentifierInfo *name,
                                        Node type, Node target);

    void acceptDeclarationInitializer(Node declNode, Node initializer);

    bool acceptUseDeclaration(Node usedType);

    Node acceptProcedureCall(Node name);

    Node acceptIntegerLiteral(llvm::APInt &value, Location loc);

    Node acceptStringLiteral(const char *string, unsigned len, Location loc);

    Node acceptNullExpr(Location loc);

    Node acceptAllocatorExpr(Node operand, Location loc);

    Node acceptQualifiedExpr(Node qualifier, Node operand);

    Node acceptDereference(Node prefix, Location loc);

    Node acceptIfStmt(Location loc, Node condition, NodeVector &consequents);

    Node acceptElseStmt(Location loc, Node ifNode, NodeVector &alternates);

    Node acceptElsifStmt(Location loc, Node ifNode, Node condition,
                         NodeVector &consequents);

    Node acceptEmptyReturnStmt(Location loc);

    Node acceptReturnStmt(Location loc, Node retNode);

    Node acceptAssignmentStmt(Node target, Node value);

    Node beginBlockStmt(Location loc, IdentifierInfo *label = 0);
    void endBlockStmt(Node block);

    Node beginHandlerStmt(Location loc, NodeVector &choices);
    void endHandlerStmt(Node context, Node handler);

    Node acceptNullStmt(Location loc);

    bool acceptStmt(Node context, Node stmt);

    Node acceptRaiseStmt(Location loc, Node exception, Node message);

    Node acceptPragmaStmt(IdentifierInfo *name, Location loc, NodeVector &args);

    void acceptPragmaImport(Location pragmaLoc,
                            IdentifierInfo *convention, Location conventionLoc,
                            IdentifierInfo *entity, Location entityLoc,
                            Node externalNameNode);

    void beginEnumeration(IdentifierInfo *name, Location loc);
    void acceptEnumerationIdentifier(IdentifierInfo *name, Location loc);
    void acceptEnumerationCharacter(IdentifierInfo *name, Location loc);
    void endEnumeration();

    void acceptIntegerTypeDecl(IdentifierInfo *name, Location loc,
                               Node low, Node high);
    void acceptModularTypeDecl(IdentifierInfo *name, Location loc,
                               Node modulus);

    void acceptRangedSubtypeDecl(IdentifierInfo *name, Location loc,
                                 Node subtype, Node low, Node high);

    void acceptSubtypeDecl(IdentifierInfo *name, Location loc, Node subtype);
    void acceptIncompleteTypeDecl(IdentifierInfo *name, Location loc);
    void acceptAccessTypeDecl(IdentifierInfo *name, Location loc, Node subtype);

    void acceptArrayDecl(IdentifierInfo *name, Location loc,
                         NodeVector indices, Node component);

    void beginRecord(IdentifierInfo *name, Location loc);
    void acceptRecordComponent(IdentifierInfo *name, Location loc, Node type);
    void endRecord();

    void acceptPrivateTypeDecl(IdentifierInfo *name, Location loc,
                               unsigned typeTag);

    void acceptDerivedTypeDecl(IdentifierInfo *name, Location loc,
                               Node parentNode);

    // Delete the underlying Ast node.
    void deleteNode(Node &node);
    //@}

    /// \name Generic Accessors and Predicates.
    ///
    //@{

    /// \brief Returns true if the type checker has not encountered an error and
    /// false otherwise.
    bool checkSuccessful() const { return diagnostic.numErrors() == 0; }

    /// Returns the compilation which this type checker populates with well
    /// formed top-level nodes.
    CompilationUnit *getCompilationUnit() const { return compUnit; }

    /// Returns the Diagnostic object thru which diagnostics are posted.
    Diagnostic &getDiagnostic() { return diagnostic; }

    /// Returns the AstResource used by the type checker to construct AST nodes.
    AstResource &getAstResource() { return resource; }

    /// Returns the TextManager backing all sources being processed.
    TextManager &getTextManager() { return manager; }
    //@}

    /// \name General Semantic Analysis.
    //@{

    /// Returns true if the type \p source requires a conversion to be
    /// compatible with the type \p target.
    bool conversionRequired(Type *source, Type *target);

    /// Wraps the given expression in a ConversionExpr if needed.
    Expr *convertIfNeeded(Expr *expr, Type *target);

    /// Returns a dereferenced type of \p source which covers the type \p target
    /// or null if no such type exists.
    Type *getCoveringDereference(Type *source, Type *target);

    /// Returns a dereferenced type of \p source which satisfies the given
    /// target classification or null if no such type exists.
    Type *getCoveringDereference(Type *source, Type::Classification ID);

    /// Implicitly wraps the given expression in DereferenceExpr nodes utill its
    /// type covers \p target.
    ///
    /// The given expression must have an access type which can be dereferenced
    /// to the given target type.
    Expr *implicitlyDereference(Expr *expr, Type *target);

    /// Implicitly wraps the given expression in DereferenceExpr nodes utill its
    /// type satisfies the given type classification.
    ///
    /// The given expression must have an access type which can be dereferenced
    /// to yield a type beloging to the given classification.
    Expr *implicitlyDereference(Expr *expr, Type::Classification ID);

    /// \brief Typechecks the given expression in the given type context.
    ///
    /// This is a main entry point into the top-down phase of the type checker.
    /// This method returns a potentially different expression from its argument
    /// on success and null on failure.
    Expr *checkExprInContext(Expr *expr, Type *context);

    /// Typechecks the given expression using the given type classification as
    /// context.  This method returns true if the expression was successfully
    /// checked.  Otherwise, false is returned an diagnostics are emitted.
    bool checkExprInContext(Expr *expr, Type::Classification ID);

    /// Typechecks the given \em resolved expression in the given type context
    /// introducing any implicit dereferences required to conform to the context
    /// type.
    ///
    /// Returns the (possibly updated) expression on success or null on error.
    Expr *checkExprAndDereferenceInContext(Expr *expr, Type *context);

    /// Returns true if \p expr is a static integer expression.  If so,
    /// initializes \p result to a signed value which can accommodate the given
    /// static expression.
    bool ensureStaticIntegerExpr(Expr *expr, llvm::APInt &result);

    /// Returns true if \p expr is a static integer expression.  Otherwise false
    /// is returned and diagnostics are posted.
    bool ensureStaticIntegerExpr(Expr *expr);

    /// Returns true if \p expr is a positive static integer expression.
    /// Otherwise false is returned and diagnostics are posted.
    bool ensurePositiveIntegerExpr(Expr *expr);

    /// Checks if \p node resolves to an expression and returns that expression
    /// on success.  Else null is returned and diagnostics are posted.
    Expr *ensureExpr(Node node);

    /// Checks if \p node resolves to an expression and returns that expression
    /// on success.  Else null is returned and diagnostics are posted.
    Expr *ensureExpr(Ast *node);

    /// Checks that the given identifier is a valid direct name.  Returns an Ast
    /// node representing the name if the checks succeed and null otherwise.
    ///
    /// \param name The identifier to check.
    ///
    /// \param loc The location of \p name.
    ///
    /// \param forStatement If true, check that \p name denotes a procedure.
    Ast *checkDirectName(IdentifierInfo *name, Location loc, bool forStatement);

    /// Completes a node representing a name.
    ///
    /// FIXME: This method is public to supplement checkDirectName.  Would be
    /// better to provide a replacement for checkDirectName which calls
    /// finishName when needed instead of placing the burden on the caller (see
    /// ArrayAggChecker::convertAggregateIdentifiers for an example).
    ///
    /// \see ParseClient::finishName.
    Ast *finishName(Ast *node);

    /// Basic type equality predicate.
    ///
    /// Note that this function is context sensitive.  In particular, it can use
    /// the current declarative region to determine if the types \p A and \p B
    /// cover.
    bool covers(Type *A, Type *B);
    //@}

private:
    TextManager     &manager;
    Diagnostic      &diagnostic;
    AstResource     &resource;
    CompilationUnit *compUnit;
    DeclRegion      *declarativeRegion;
    PackageDecl     *currentPackage;
    Scope            scope;

    /// Stencil classes used to hold intermediate results.
    EnumDeclStencil enumStencil;
    SRDeclStencil routineStencil;

    /// Aggregates can nest.  The following stack is used to maintain the
    /// current context when processing aggregate expressions.
    std::stack<AggregateExpr*> aggregateStack;

    /// Stack of active loop statements.
    ///
    /// On calls to begin{For, Loop, While}Stmt the corresponding node is pushed
    /// onto this stack.  This enables certain control flow checks.
    typedef llvm::SmallVector<IterationStmt*, 8> ActiveLoopSet;
    ActiveLoopSet activeLoops;

    /// Several support routines operate over llvm::SmallVector's.  Define a
    /// generic shorthand.
    template <class T>
    struct SVImpl {
        typedef llvm::SmallVectorImpl<T> Type;
    };

    //===------------------------------------------------------------------===//
    // Utility functions.
    //===------------------------------------------------------------------===//

    // Converts a Node to the corresponding Ast type.  If the node is not of the
    // supplied type, this function returns 0.
    template <class T>
    static T *lift_node(Node &node) {
        return llvm::dyn_cast_or_null<T>(Node::lift<Ast>(node));
    }

    // Casts the given Node to the corresponding type and asserts that the
    // conversion was successful.
    template <class T>
    static T *cast_node(Node &node) {
        return llvm::cast<T>(Node::lift<Ast>(node));
    }

    // A function object version of lift_node.
    template <class T>
    struct NodeLifter : public std::unary_function<Node&, T*> {
        T* operator ()(Node &node) const { return lift_node<T>(node); }
    };

    // A function object version of cast_node.
    template <class T>
    struct NodeCaster : public std::unary_function<Node&, T*> {
        T* operator ()(Node &node) const { return cast_node<T>(node); }
    };

    DeclRegion *currentDeclarativeRegion() const {
        return declarativeRegion;
    }

    void pushDeclarativeRegion(DeclRegion *region) {
        declarativeRegion = region;
    }

    DeclRegion *popDeclarativeRegion() {
        DeclRegion *res = declarativeRegion;
        declarativeRegion = res->getParent();
        return res;
    }

    CompilationUnit *currentCompUnit() const { return compUnit; }

    //@{
    /// Accessors to the current (innermost) type checking context.  If the
    /// current context is not of the requested type null is returned.
    PackageDecl *getCurrentPackage() const;
    SubroutineDecl *getCurrentSubroutine() const;
    ProcedureDecl *getCurrentProcedure() const;
    FunctionDecl *getCurrentFunction() const;
    //@}

    //@{
    /// Predicate methods returning true if we are currently processing the
    /// a given type of object.
    bool checkingPackage() const { return getCurrentPackage() != 0; }
    bool checkingProcedure() const { return getCurrentProcedure() != 0; }
    bool checkingFunction() const { return getCurrentFunction() != 0; }
    //@}

    // Called when then type checker is constructed.  Populates the top level
    // scope with the default environment specified by Comma (declarations of
    // primitive types like Boolean, for example).
    void populateInitialEnvironment();

    /// Returns true if the subroutines \p X and \p Y are compatible.
    ///
    /// This is a stronger test than just type equality.  Two subroutine
    /// declarations are compatible if:
    ///
    ///    - They both have the same name,
    ///
    ///    - they both have the same type.
    ///
    ///    - they both have the same parameter mode profile, and,
    ///
    ///    - they both have the same keywords.
    bool compatibleSubroutineDecls(SubroutineDecl *X, SubroutineDecl *Y);

    /// Brings the implicit declarations provided by \p decl into scope.
    void acquireImplicitDeclarations(Decl *decl);

    /// Ensures the given Node resolves to a complete type declaration.
    ///
    /// \see ensureCompleteTypeDecl(Decl, Location, bool);
    TypeDecl *ensureCompleteTypeDecl(Node refNode, bool report = true);

    /// Ensures that the given declaration node denotes a complete type
    /// declaration.
    ///
    /// \param decl The declaration node to check.
    ///
    /// \param loc The location to use for any diagnostic messages.
    ///
    /// \param report When true and the type could not be resolved to a complete
    /// type declaration diagnostics are posted.
    ///
    /// \return The complete type declaration if resolvable else null.
    TypeDecl *ensureCompleteTypeDecl(Decl *decl, Location loc,
                                     bool report = true);

    /// Ensures that the given declaration node denotes a type declaration.
    ///
    /// This method is less strict than ensureCompleteTypeDecl as it does note
    /// check that the given declaration denotes a complete type.
    TypeDecl *ensureTypeDecl(Decl *decl, Location loc, bool report = true);

    /// Ensures the given Node resolves to a type declaration.
    ///
    /// \see ensumeTypeDecl(Decl, Location, bool);
    TypeDecl *ensureTypeDecl(Node refNode, bool report = true);

    /// Similar to ensureCompleteTypeDecl, but operates over type nodes.
    Type *resolveType(Type *type) const;

    /// Resolves the type of the given expression.
    Type *resolveType(Expr *expr) const { return resolveType(expr->getType()); }

    // Resolves the type of the given integer literal, and ensures that the
    // given type context is itself compatible with the literal provided.
    // Returns a valid expression node (possibly different from \p intLit) if
    // the literal was successfully checked.  Otherwise, null is returned and
    // appropriate diagnostics are posted.
    Expr *resolveIntegerLiteral(IntegerLiteral *intLit, Type *context);

    // Resolved the type of the given integer literal with respect to the given
    // type classification.  Returns true if the literal was successfully
    // checked.  Otherwise false is returned and appropriate diagnostics are
    // posted.
    bool resolveIntegerLiteral(IntegerLiteral *intLit, Type::Classification ID);

    // Resolves the type of the given string literal, and ensures that the given
    // type context is itself compatible with the literal provided.  Returns a
    // valid expression if the literal was successfully checked (possibly
    // different from \p strLit).  Otherwise, null is returned and appropriate
    // diagnostics are posted.
    Expr *resolveStringLiteral(StringLiteral *strLit, Type *context);

    // Resolves the type of the an aggregate expression with respect to the
    // given type context.  Returns a valid expression if the aggregate was
    // successfully checked (possibly different from \p agg).  Otherwise, null
    // is returned and appropriate diagnostics are posted.
    Expr *resolveAggregateExpr(AggregateExpr *agg, Type *context);

    // Resolves a null expression if possible given a target type.  Returns a
    // valid expression node if \p expr was found to be compatible with \p
    // context.  Otherwise null is returned and diagnostics are posted.
    Expr *resolveNullExpr(NullExpr *expr, Type *context);

    // Resolves an allocator expression with respect to the given target type.
    // Returns a valid expression node on success and null otherwise.
    Expr *resolveAllocatorExpr(AllocatorExpr *alloc, Type *context);

    // Resolves a selected component expression with respect to the given target
    // type.
    Expr *resolveSelectedExpr(SelectedExpr *select, Type *context);

    // Resolves a diamond expression to the given target type.
    Expr *resolveDiamondExpr(DiamondExpr *diamond, Type *context);

    // Resolves an attribute expression to the given target type.
    Expr *resolveAttribExpr(AttribExpr *attrib, Type *context);

    // Resolves the given call expression to one which satisfies the given
    // target type.  Returns a valid expression if the call was successfully
    // checked (possibly different from \p call).  Otherwise, null is returned
    // and the appropriate diagnostics are emitted.
    Expr *resolveFunctionCall(FunctionCallExpr *call, Type *type);

    // Resolves the given call expression to one which satisfies the given type
    // classification and returns true.  Otherwise, false is returned and the
    // appropriate diagnostics are emitted.
    bool resolveFunctionCall(FunctionCallExpr *call, Type::Classification ID);

    // Resolves the given call expression to one which returns a record type
    // that in turn provides a component with the given name and type.  Returns
    // a valid expression if the call was successfully resolved (possibly
    // different from \p call).  Otherwise, null is returned and diagnostics are
    // posted.
    Expr *resolveFunctionCall(FunctionCallExpr *call, IdentifierInfo *selector,
                              Type *targetType);

    // Returns the prefered connective for the given ambiguous function call, or
    // null if no unambiguous interpretation exists.
    FunctionDecl *resolvePreferredConnective(FunctionCallExpr *Call,
                                             Type *targetType);

    // Checks if the given set of function declarations contains a preferred
    // primitive operator which should be preferred over any other.  Returns the
    // prefered declaration if found and null otherwise.
    FunctionDecl *resolvePreferredOperator(SVImpl<FunctionDecl*>::Type &decls);

    /// Checks that the given SubroutineRef can be applied to the given argument
    /// nodes.
    ///
    /// \return An SubroutineCall node representing the result of the
    /// application, an IndexedArrayExpr, or null if the call did not type
    /// check.
    Ast *acceptSubroutineApplication(SubroutineRef *ref,
                                     NodeVector &argNodes);

    /// Given a vector \p argNodes of Node's representing the arguments to a
    /// subroutine call, extracts the AST nodes and fills in the vectors \p
    /// positional and \p keyed with the positional and keyed arguments,
    /// respectively.
    ///
    /// This method ensures that the argument nodes are generally compatible
    /// with a subroutine call; namely, that all positional and keyed arguments
    /// denote Expr's.  If this conversion fails, diagnostics are posted, false
    /// is returned, and the contents of the given vectors is undefined.
    bool checkSubroutineArgumentNodes(NodeVector &argNodes,
                                      SVImpl<Expr*>::Type &positional,
                                      SVImpl<KeywordSelector*>::Type &keyed);

    /// Checks that the given expression \p arg satisifes the type \p
    /// targetType.  Also ensures that \p arg is compatible with the given
    /// parameter mode.  Returns a possibly updated expression node if the check
    /// succeed and null otherwise.
    Expr *checkSubroutineArgument(Expr *arg, Type *targetType,
                                  PM::ParameterMode targetMode);

    /// Applys checkSubroutineArgument() to each argument of the given call node
    /// (which must be resolved).
    bool checkSubroutineCallArguments(SubroutineCall *call);

    /// Checks that given arguments are compatible with those of the given
    /// decl.
    ///
    /// It is assumed that the number of arguments passed matches the number
    /// expected by the decl.  Each argument is checked for type and mode
    /// compatibility.  Also, each keyed argument is checked to ensure that the
    /// key exists, that the argument does not conflict with a positional
    /// parameter, and that all keys are unique.  Returns true if the check
    /// succeeds, false otherwise and appropriate diagnostics are posted.
    bool checkSubroutineArguments(SubroutineDecl *decl,
                                  SVImpl<Expr*>::Type &posArgs,
                                  SVImpl<KeywordSelector*>::Type &keyArgs);

    /// Checks that the given subroutine decl accepts the provided positional
    /// arguments.
    bool routineAcceptsArgs(SubroutineDecl *decl,
                            SVImpl<Expr*>::Type &args);

    /// Checks that the given subroutine decl accepts the provided keyword
    /// arguments.
    bool routineAcceptsArgs(SubroutineDecl *decl,
                            SVImpl<KeywordSelector*>::Type &args);

    /// Checks a possibly ambiguous (overloaded) SubroutineRef \p ref given a
    /// set of positional and keyed arguments.
    ///
    /// \return A general AST node which is either a FunctionCallExpr,
    /// ProcedureCallStmt, ArrayIndexExpr, or null if the call did not type
    /// check.
    Ast *acceptSubroutineCall(SubroutineRef *ref,
                              SVImpl<Expr*>::Type &positionalArgs,
                              SVImpl<KeywordSelector*>::Type &keyedArgs);

    /// Given a resolved (not overloaded) SubroutineRef \p ref, and a set of
    /// positional and keyed arguments, checks the arguments and builds an AST
    /// node representing the call.
    ///
    /// \return A general AST node which is either a FunctionCallExpr,
    /// ProcedureCallStmt, or null if the call did not type check.
    SubroutineCall *
    checkSubroutineCall(SubroutineRef *ref,
                        SVImpl<Expr*>::Type &positionalArgs,
                        SVImpl<KeywordSelector*>::Type &keyArgs);

    /// Injects implicit ConversionExpr nodes into the positional

    /// Returns true if \p expr is compatible with the given type.
    ///
    /// This routine does not post diagnostics.  It is used to do a speculative
    /// check of a subroutine argument when resolving a set of overloaded
    /// declarations.
    bool checkApplicableArgument(Expr *expr, Type *targetType);

    // Verifies that the given BodyDecl satisfies the constraints imposed by its
    // specification.  Returns true if the constraints are satisfied.
    // Otherwise, false is returned and diagnostics are posted.
    bool ensureExportConstraints(BodyDecl *add);

    // Verifies that the given package declaration provides all necessary
    // declarations in its private part to satisfy its specification.
    bool ensurePrivateConstraints(PackageDecl *package);

    /// Returns true if the given parameter is of mode "in", and thus capatable
    /// with a function declaration.  Otherwise false is returned an a
    /// diagnostic is posted.
    bool checkFunctionParameter(ParamValueDecl *param);

    /// Helper for acceptEnumerationIdentifier and acceptEnumerationCharacter.
    /// Forms a generic enumeration literal AST node.  Returns true if the
    /// literal was generated successfully.
    bool acceptEnumerationLiteral(IdentifierInfo *name, Location loc);

    /// Adds a declarative region to the current scope as direct names.
    void introduceDeclRegion(DeclRegion *region);

    /// Adds the declarations present in the given region to the current scope
    /// as direct names.  This subroutine is used to introduce the implicit
    /// operations which accompany a type declaration.  If the region introduces
    /// any conflicting names a diagnostic is posted and the corresponding
    /// declaration is not added.
    void introduceImplicitDecls(DeclRegion *region);

    /// Processes the indirect names in the given resolver.  If no indirect
    /// names could be found, a diagnostic is posted and and null is returned.
    Ast *checkIndirectName(Location loc, Resolver &resolver);

    /// Introduces the given type declaration node into the current scope and
    /// declarative region.
    ///
    /// This method performs several checks.  First, it ensures that the
    /// declaration does not conflict with any other declaration in the current
    /// scope.  Second, if the given declaration can serve as a completion for a
    /// visible incomplete type declaration the necessary updates to the
    /// corresponding incomplete declaration node is performed.
    ///
    /// \return True if the declaration was successfully added into the scope
    /// and current declarative region.  Otherwise false is returned and
    /// diagnostics are posted.
    bool introduceTypeDeclaration(TypeDecl *decl);

    /// Builds an IndexedArrayExpr.
    ///
    /// Checks that \p expr is of array type, and ensures that the given
    /// argument nodes can serve as indexes into \p expr.
    ///
    /// \return An IndexedArrayExpr if the expression is valid and null
    /// otherwise.
    IndexedArrayExpr *acceptIndexedArray(Expr *ref, NodeVector &argNodes);

    /// Checks that the given nodes are generally valid as array indices
    /// (meaning that they must all resolve to denote Expr's).  Fills in the
    /// array \p indices with the results.  If the conversion fails, then false
    /// is returned, diagnostics are posted, and the state of \p indices is
    /// undefined.
    bool checkArrayIndexNodes(NodeVector &argNodes,
                              SVImpl<Expr*>::Type &indices);

    /// Typechecks an indexed array expression.
    ///
    /// \return An IndexedArrayExpr on success or null on failure.
    IndexedArrayExpr *acceptIndexedArray(Expr *ref,
                                         SVImpl<Expr*>::Type &indices);

    /// Checks an object declaration of array type.  This method is a special
    /// case version of acceptObjectDeclaration().
    ///
    /// \param loc The location of the declaration.
    ///
    /// \param name The object identifier.
    ///
    /// \param arrTy Type corresponding to the object.
    ///
    /// \param init Initialization expression, or null if there is no
    /// initializer.
    ///
    /// \return An ObjectDecl node or null if the declaration did not type
    /// check.
    ObjectDecl *acceptArrayObjectDeclaration(Location loc, IdentifierInfo *name,
                                             ArrayType *arrTy, Expr *init);

    /// Returns a constrained array subtype derived from the given type \p arrTy
    /// and an array valued expression (typically an array initializer) \p init.
    ArrayType *getConstrainedArraySubtype(ArrayType *arrTy, Expr *init);

    /// Creates a FunctionCallExpr or ProcedureCallStmt representing the given
    /// SubroutineRef, provided that \p ref admits declarations with arity zero.
    /// On failure, null is returned and diagnostics posted.
    Ast *finishSubroutineRef(SubroutineRef *ref);

    /// Helper method for acceptSelectedComponent.
    Ast *processExpandedName(DeclRegion *region,
                             IdentifierInfo *name, Location loc,
                             bool forStatement);

    /// Helper method for acceptSelectedComponent.  Handles the case when the
    /// prefix denotes an expression.
    Ast *processSelectedComponent(Expr *expr, IdentifierInfo *name,
                                  Location loc, bool forStatement);

    /// Checks an assert pragma with the given arguments.
    PragmaAssert *acceptPragmaAssert(Location loc, NodeVector &args);

    Ast *checkAttribute(attrib::AttributeID, Ast *prefix, Location loc);

    /// Checks a type conversion expression.
    ConversionExpr *acceptConversionExpr(TypeRef *prefix, NodeVector &args);

    /// Returns the location of \p node.
    static Location getNodeLoc(Node node);

    SourceLocation getSourceLoc(Location loc) const {
        return manager.getSourceLocation(loc);
    }

    DiagnosticStream &report(Location loc, diag::Kind kind) {
        return diagnostic.report(getSourceLoc(loc), kind);
    }
};

} // End comma namespace

#endif
