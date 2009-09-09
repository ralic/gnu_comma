//===-- parser/ParseClient.h ---------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008-2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#ifndef COMMA_PARSECLIENT_HDR_GUARD
#define COMMA_PARSECLIENT_HDR_GUARD

#include "comma/basic/ParameterModes.h"
#include "comma/basic/IdentifierInfo.h"
#include "comma/parser/Descriptors.h"

namespace llvm {

class APInt;

} // end llvm namespace

namespace comma {

class ParseClient {

public:
    /// Nodes cannot be constructed from outside a parse client, yet many of the
    /// callbacks take null nodes indicateing a non-existant argument.  This
    /// method is made available to the parser so that it has a means of
    /// producing such null nodes.
    Node getNullNode() { return Node::getNullNode(this); }

    /// Nodes cannot be constructed from outside a parse client.  However, the
    /// parser needs to be able to create invalid nodes to communicate failures
    /// during parsing (just as the ParseClient returns invalid nodes to
    /// indicate a semantic failure).
    Node getInvalidNode() { return Node::getInvalidNode(this); }

    /// Nodes do not know the representation of the data they carry.  This
    /// method is called by Nodes once their reference counts drop to zero.  The
    /// implementation need not delete the node as a result of this call -- it
    /// might choose to cache it, for instance.
    virtual void deleteNode(Node &node) = 0;

    /// \name Initial Callbacks.
    ///
    /// When a top-level capsule is about to be parsed, beginCapsule is invoked
    /// to notify the client of the processing to come.  This is the first
    /// callback the parser ever invokes on its client.  Once the capsule has
    /// been parsed (successfully or not), endCapsule is called.
    ///
    ///@{
    virtual void beginCapsule() = 0;
    virtual void endCapsule() = 0;
    ///@}

    /// \name Generic Formal Callbacks.
    ///
    /// The following callbacks are invoked when processing generic formal
    /// parameters.
    ///
    ///@{
    ///
    /// \name Generic Formal Delimiters.
    ///
    /// Processing of a generic formal part begins with a call to
    /// beginGenericFormals and completes with a call to endGenericFormals.
    /// These calls delimit the scope of a generic formal part to the client.
    ///
    /// @{
    virtual void beginGenericFormals() = 0;
    virtual void endGenericFormals() = 0;
    ///@}

    /// \name Formal Domain Declarations.
    ///
    /// The client is notified of the start of a formal domain declaration with
    /// a call to beginFormalDomainDecl, giving the name and location of the
    /// declaration.  The super signatures and component declarations are then
    /// processed.  The end of a formal domain declaration is announced with a
    /// call to endFormalDomainDecl.
    ///
    ///@{
    virtual void beginFormalDomainDecl(IdentifierInfo *name, Location loc) = 0;
    virtual void endFormalDomainDecl() = 0;
    ///@}
    ///@}

    /// \name Capsule Callbacks.
    ///
    /// Once the generic formal part has been processed (if present at all), one
    /// following callbacks is invoked to inform the client of the type and name
    /// of the upcomming capsule.  The context established by these callbacks is
    /// terminated when endCapsule is called.
    ///
    ///@{
    virtual void beginDomainDecl(IdentifierInfo *name, Location loc) = 0;
    virtual void beginSignatureDecl(IdentifierInfo *name, Location loc) = 0;
    ///@}

    /// \name Signature Profile Callbacks.
    ///
    ///@{
    ///
    /// \name Signature Profile Delimiters
    ///
    /// When the signature profile of a top-level capsule or generic formal
    /// domain is about to be processed, beginSignatureProfile is called.  Once
    /// the processing of the profile is finished (regarless of whether the
    /// parse was successful or not) endSignatureProfile is called.
    ///
    ///@{
    virtual void beginSignatureProfile() = 0;
    virtual void endSignatureProfile() = 0;
    ///@}

    /// Called for each super signature defined in a signature profile.
    virtual void acceptSupersignature(Node typeNode, Location loc) = 0;
    ///@}

    /// Called at the begining of an add expression.  The client accepts
    /// components of an add expression after this call until endAddExpression
    /// is called.
    virtual void beginAddExpression() = 0;

    /// Completes an add expression.
    virtual void endAddExpression() = 0;

    /// Invoked when the parser consumes a carrier declaration.
    virtual void acceptCarrier(IdentifierInfo *name, Node typeNode,
                               Location loc) = 0;


    /// \name Subroutine Declaration Callbacks.
    ///
    /// When a subroutine declaration is about to be parsed, either
    /// beginFunctionDeclaration or beginProcedureDeclaration is invoked to
    /// inform the client of the kind and name of the upcomming subroutine.
    /// Once the declaration has been processed, the context is terminated with
    /// a call to endSubroutineDeclaration.
    ///
    ///@{
    virtual void beginFunctionDeclaration(IdentifierInfo *name,
                                          Location loc) = 0;
    virtual void beginProcedureDeclaration(IdentifierInfo *name,
                                           Location loc) = 0;

    /// When parsing a function declaration, this callback is invoked to notify
    /// the client of the declarations return type.
    ///
    /// If the function declaration was missing a return type, or the node
    /// returned by the client representing the type is invalid, this callback
    /// is passed a null node as argument.  Note that the parser posts a
    /// diagnostic for the case of a missing return.
    virtual void acceptFunctionReturnType(Node typeNode) = 0;

    /// For each subroutine parameter, acceptSubroutineParameter is called
    /// providing:
    ///
    /// \param formal The name of the formal parameter.
    ///
    /// \param loc The location of the formal parameter name.
    ///
    /// \param typeNode A node describing the type of the parameter (the result
    ///  of a call to acceptTypeName or acceptTypeApplication, for example).
    ///
    /// \param mode The parameter mode, wher PM::MODE_DEFAULT is supplied if
    ///  an explicit mode was not parsed.
    ///
    virtual void acceptSubroutineParameter(IdentifierInfo *formal, Location loc,
                                           Node typeNode,
                                           PM::ParameterMode mode) = 0;

    /// Called to notify the client that a subroutine was declared as an
    /// overriding declaration.  The only time this callback is invoked by the
    /// parser is when it is processing a signature profile.
    ///
    /// \param qualNode A node resulting from a call to acceptQualifier or
    /// acceptNestedQualifier, representing the qualification of \p name, or a
    /// null Node if there was no qualification.
    ///
    /// \param name An IdentifierInfo naming the target of the override.
    ///
    /// \param loc The location of \p name.
    virtual void acceptOverrideTarget(Node qualNode,
                                      IdentifierInfo *name, Location loc) = 0;

    /// Called to terminate the context of a subroutine declaration.
    ///
    /// \param definitionFollows Set to true if the parser sees a \c is token
    /// following the declaration and thus expects a definition to follow.
    ///
    /// \return A node associated with the declaration.  Exclusively used by the
    /// parser as an argument to beginSubroutineDefinition.
    virtual Node endSubroutineDeclaration(bool definitionFollows) = 0;
    ///@}

    /// \name Subroutine Definition Callbacks.
    ///
    ///@{
    ///
    /// Once a declaration has been parsed, a context for a definition is
    /// introduced with a call to beginSubroutineDefinition (assuming the
    /// declaration has a definition), passing in the node returned from
    /// endSubroutineDeclaration.
    virtual void beginSubroutineDefinition(Node declarationNode) = 0;

    /// For each statement consituting the body of a definition
    /// acceptSubroutineStmt is invoked with the node provided by any one of the
    /// statement callbacks (acceptIfStmt, acceptReturnStmt, etc) provided that
    /// the Node is valid.  Otherwise, the Node is dropped and this callback is
    /// not invoked.
    virtual void acceptSubroutineStmt(Node stmt) = 0;

    /// Once the body of a subroutine has been parsed, this callback is invoked
    /// to singnal the completion of the definition.
    virtual void endSubroutineDefinition() = 0;
    /// @}

    virtual bool acceptObjectDeclaration(Location loc, IdentifierInfo *name,
                                         Node type, Node initializer) = 0;

    virtual Node acceptPercent(Location loc) = 0;

    virtual Node acceptTypeName(IdentifierInfo *info, Location loc,
                                Node qualNode) = 0;

    virtual Node acceptTypeApplication(IdentifierInfo *connective,
                                       NodeVector &argumentNodes,
                                       Location *argumentLocs,
                                       IdentifierInfo **keys,
                                       Location *keyLocs,
                                       unsigned numKeys,
                                       Location loc) = 0;

    virtual Node acceptKeywordSelector(IdentifierInfo *key, Location loc,
                                       Node exprNode, bool forSubroutine) = 0;

    virtual Node acceptDirectName(IdentifierInfo *name, Location loc,
                                  Node qualNode) = 0;

    virtual Node acceptFunctionName(IdentifierInfo *name, Location loc,
                                    Node qualNode) = 0;

    virtual Node acceptFunctionCall(Node connective, Location loc,
                                    NodeVector &args) = 0;

    virtual Node acceptProcedureName(IdentifierInfo *name, Location loc,
                                     Node qualNode) = 0;

    virtual Node acceptProcedureCall(Node connective, Location loc,
                                     NodeVector &args) = 0;

    /// Called for "inj" expressions.  loc is the location of the inj token and
    /// expr is its argument.
    virtual Node acceptInj(Location loc, Node expr) = 0;

    /// Called for "prj" expressions.  loc is the location of the prj token and
    /// expr is its argument.
    virtual Node acceptPrj(Location loc, Node expr) = 0;

    virtual Node acceptQualifier(Node qualifierType, Location loc) = 0;

    virtual Node acceptNestedQualifier(Node qualifier, Node qualifierType,
                                       Location loc) = 0;

    virtual Node acceptIntegerLiteral(llvm::APInt &value, Location loc) = 0;

    /// Submits an import from the given type node, and the location of the
    /// import reserved word.
    virtual bool acceptImportDeclaration(Node importedType, Location loc) = 0;

    virtual Node acceptIfStmt(Location loc, Node condition,
                              NodeVector &consequents) = 0;

    virtual Node acceptElseStmt(Location loc, Node ifNode,
                                NodeVector &alternates) = 0;

    virtual Node acceptElsifStmt(Location loc, Node ifNode, Node condition,
                                 NodeVector &consequents) = 0;

    virtual Node acceptEmptyReturnStmt(Location loc) = 0;

    virtual Node acceptReturnStmt(Location loc, Node retNode) = 0;

    virtual Node acceptAssignmentStmt(Location loc, IdentifierInfo *target,
                                      Node value) = 0;

    /// Called when a block statement is about to be parsed.
    virtual Node beginBlockStmt(Location loc, IdentifierInfo *label = 0) = 0;

    /// This method is called for each statement associated with the block.
    virtual void acceptBlockStmt(Node block, Node stmt) = 0;

    /// Once the last statement of a block has been parsed, this method is
    /// called to inform the client that we are leaving the block context
    /// established by the last call to beginBlockStmt.
    virtual void endBlockStmt(Node block) = 0;

    /// Called to inform the client of a while statement.
    virtual Node acceptWhileStmt(Location loc, Node condition,
                                 NodeVector &stmtNodes) = 0;

    /// Called when an enumeration type is about to be parsed, supplying the
    /// name of the type and its location.  For each literal composing the
    /// enumeration, acceptEnumerationLiteral is called with the result of this
    /// function.
    virtual Node beginEnumerationType(IdentifierInfo *name, Location loc) = 0;

    /// Called for each literal composing an enumeration type, where the first
    /// argument is a valid node as returned by acceptEnumerationType.
    virtual void acceptEnumerationLiteral(Node enumeration, IdentifierInfo *name,
                                          Location loc) = 0;

    /// Called when all of the enumeration literals have been processed, thus
    /// completing the definition of the enumeration.
    virtual void endEnumerationType(Node enumeration) = 0;

    /// Called to process integer type definitions.
    ///
    /// For example, given a definition of the form <tt>type T is range
    /// X..Y;</tt>, this callback is invoked with \p name set to the identifier
    /// \c T, \p loc set to the location of \p name, \p low set to the
    /// expression \c X, and \p high set to the expression \c Y.
    virtual void acceptIntegerTypedef(IdentifierInfo *name, Location loc,
                                      Node low, Node high) = 0;

protected:
    /// Allow sub-classes to construct arbitrary nodes.
    Node getNode(void *ptr) { return Node(this, ptr); }

    /// Construct a node which has released its ownership to the associated
    /// data.
    Node getReleasedNode(void *ptr) {
        Node node(this, ptr);
        node.release();
        return node;
    }
};

} // End comma namespace.

#endif
