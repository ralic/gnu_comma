//===-- parser/ParseClient.h ---------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008-2010, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#ifndef COMMA_PARSECLIENT_HDR_GUARD
#define COMMA_PARSECLIENT_HDR_GUARD

#include "comma/basic/ParameterModes.h"
#include "comma/basic/IdentifierInfo.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/PointerIntPair.h"

namespace llvm {

class APInt;

} // end llvm namespace

namespace comma {

class ParseClient;

//===----------------------------------------------------------------------===//
// Node
//
// This class encapsulates (in a non-type-safe manner) the internal data
// structures produced by a ParseClient, and provides automatic memory
// management of that data by way of a reference counting mechanism.
//
// When a ParseClient returns data to the parser, it wraps it up in a Node
// instance.  This provides a uniform, opaque handle on the data which the
// parser can collect and submit back to the ParseClient.
//
// A Node provides automatic memory management using a reference counting
// mechanism.  Nodes can be freely copied and assigned to.  Each Node contains a
// pointer to the ParseClient which produced its associated data.  When the
// reference count drops to zero, ParseClient::deleteNode is called with the
// Node about to be freed, giving the ParseClient the opportunity to manage to
// allocation of its data.
//
// In general, automatic reclamation of nodes occurs when an error is
// encountered during parsing, or during the analysis performed by the
// ParseClient itself.  When the ParseClient accepts a particular construct that
// the parser produces, the associated Node's are typically released, thereby
// inhibiting the automatic reclamation that would otherwise occur.
//
// A Node can be marked as "invalid", meaning that the data which is associated
// with the Node is malformed in some respect.  A ParseClient can return invalid
// nodes to indicate that it could not handle a construct produced by the
// parser.  The parser in turn never submits an invalid Node back to the client.
class Node {

    // This simple structure is used to maintain the ParseClient and reference
    // count associated with each node.
    struct NodeState {

        // Disjoint properties associated with each node, recorded in the low
        // order bits of NodeState::client.
        enum Property {
            None     = 0,      ///< Empty property.
            Invalid  = 1,      ///< Node is invalid.
            Released = 2       ///< Node does not own its payload.
        };

        // The ParseClient associated with this node.  We encode the
        // NodePropertys associated with this node in the low order bits of this
        // field as it is expected to be accessed relatively infrequently.
        llvm::PointerIntPair<ParseClient *, 2> client;

        // The reference count.
        unsigned rc;

        // The payload associated with this node.
        void *payload;

        // NOTE: Constructor initializes the reference count to 1.
        NodeState(ParseClient *client,
                  void *ptr = 0, Property prop = None)
            : client(client, prop),
              rc(1),
              payload(ptr) { }

    private:
        // Do not implement.
        NodeState(const NodeState &state);
        NodeState &operator=(const NodeState &state);
    };

    // Construction of nodes is prohibited except by the ParseClient producing
    // them.  Thus, all direct constructors are private and we define the
    // ParseClient as a friend.
    Node(ParseClient *client, void *ptr, NodeState::Property prop)
        : state(new NodeState(client, ptr, prop)) { }

    Node(ParseClient *client, void *ptr = 0)
        : state(new NodeState(client, ptr)) { }

    static Node getInvalidNode(ParseClient *client) {
        return Node(client, 0, NodeState::Invalid);
    }

    static Node getNullNode(ParseClient *client) {
        return Node(client);
    }

    friend class ParseClient;

public:
    Node(const Node &node)
        : state(node.state) {
        ++state->rc;
    }

    ~Node() { dispose(); }

    Node &operator=(const Node &node);


    // Returns true if this node is invalid.
    bool isInvalid() const {
        return state->client.getInt() & NodeState::Invalid;
    }

    // Returns true if this Node is valid.
    bool isValid() const { return !isInvalid(); }

    // Marks this node as invalid.
    void markInvalid();

    // Returns true if this Node is not associated with any data.
    bool isNull() const {
        return state->payload == 0;
    }

    // Releases ownership of this node (and all copies).
    void release();

    // Returns true if this Node owns the associated pointer.
    bool isOwning();

    // Returns the reference count associated with this node.
    unsigned getRC() { return state->rc; }

    // Returns the pointer associated with this node cast to the supplied type.
    template <class T> static T *lift(Node &node) {
        return reinterpret_cast<T*>(node.state->payload);
    }

private:
    void dispose();

    // Heap-allocated state associated with this node (and all copies).
    NodeState *state;
};

//===----------------------------------------------------------------------===//
// NodeVector
//
// A simple vector type which manages a collection of Node's.  This type
// provides a release method which releases all of the Node's associated with
// the vector.
class NodeVector : public llvm::SmallVector<Node, 16> {
public:
    void release();
};

class ParseClient {

public:
    virtual ~ParseClient() { }

    /// Enumeration itemizing the various tags that can be associated with a
    /// type declartions.  Elements of this enumeration can be or'ed together to
    /// create a composite tag.
    enum TypeTag {
        AbstractTypeTag     = 1 << 0,
        TaggedTypeTag       = 1 << 1,
        LimitedTypeTag      = 1 << 2,
        SynchronizedTypeTag = 1 << 3
    };

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

    /// Called to inform the client that a with clause has been parsed.
    ///
    /// \param loc Location of the 'with' reserved word.
    ///
    /// \param names Vector of identifiers corresponding to a dotted (qualified)
    /// name.
    ///
    /// \param numNames Number of elements in \p names.
    virtual void acceptWithClause(Location loc, IdentifierInfo **names,
                                  unsigned numNames) = 0;

    /// \name Package Callbacks.
    ///
    //@{
    /// Called at the beginning of a package spec.  When this callback returns
    /// true the client must then accept a sequence of basic declarative items
    /// until either endPackageSpec or beginPackagePrivate is called.  Otherwise
    /// the package spec is skipped and endPackageSpec is not called.
    virtual bool beginPackageSpec(IdentifierInfo *name, Location loc) = 0;

    /// Called at the begining of the private part of a package.  The private
    /// part of the package is terminated with a call to endPackageSpec.
    virtual void beginPackagePrivatePart(Location loc) = 0;

    /// Completes a package spec.
    virtual void endPackageSpec() = 0;

    /// Called at the begining of a package body.  When this callback returns
    /// true the client must then accept a sequence of declarative items until
    /// endPackageBody is called.  Otherwise the package body is skipped and
    /// endPackageBody is not called.
    virtual bool beginPackageBody(IdentifierInfo *name, Location loc) = 0;

    /// Completes a package body.
    virtual void endPackageBody() = 0;
    //@}

    /// \name Subroutine Declaration Callbacks.
    ///
    /// When a subroutine declaration is about to be parsed, either
    /// beginFunctionDeclaration or beginProcedureDeclaration is invoked to
    /// inform the client of the kind and name of the upcomming subroutine.
    /// Once the declaration has been processed, the context is terminated with
    /// a call to endSubroutineDeclaration.
    ///
    //@{
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

    /// Called to terminate the context of a subroutine declaration.
    ///
    /// \param definitionFollows Set to true if the parser sees a \c is token
    /// following the declaration and thus expects a definition to follow.
    ///
    /// \return A node associated with the declaration.  Exclusively used by the
    /// parser as an argument to beginSubroutineDefinition.
    virtual Node endSubroutineDeclaration(bool definitionFollows) = 0;
    //@}

    /// \name Subroutine Definition Callbacks.
    ///
    //@{

    /// Once a declaration has been parsed, a context for a definition is
    /// introduced with a call to beginSubroutineDefinition (assuming the
    /// declaration has a definition), passing in the node returned from
    /// endSubroutineDeclaration.  The returned Node will be used as the first
    /// parameter to acceptStmt and endHandlerStmt for each statement and
    /// exception handler constituting the definition.
    virtual Node beginSubroutineDefinition(Node declarationNode) = 0;

    /// Once the sequence of statements constituting the body of the subroutine
    /// has been parsed (not counting any exception handlers), this method is
    /// invoked to inform the client of the completion of the body.
    virtual void endSubroutineBody(Node context) = 0;

    /// Once endSubroutineBody has been called and any exception handlers have
    /// been processed this callback is invoked to signal the completion of the
    /// definition.
    virtual void endSubroutineDefinition() = 0;
    //@}

    /// \name Name Callbacks
    ///
    /// \brief Callbacks concerned with the specification of Comma names.
    //@{
    virtual Node acceptDirectName(IdentifierInfo *name, Location loc,
                                  bool forStatement) = 0;

    virtual Node acceptCharacterLiteral(IdentifierInfo *lit, Location loc) = 0;

    virtual Node acceptSelectedComponent(Node prefix,
                                         IdentifierInfo *name,
                                         Location loc,
                                         bool forStatement) = 0;

    virtual Node acceptParameterAssociation(IdentifierInfo *key,
                                            Location loc, Node rhs) = 0;

    virtual Node acceptApplication(Node prefix, NodeVector &argumentNodes) = 0;

    virtual Node acceptAttribute(Node prefix,
                                 IdentifierInfo *name, Location loc) = 0;

    virtual Node finishName(Node name) = 0;
    //@}

    /// \name Aggregate Callbacks.
    ///
    /// Processing of an aggregate expresion is bracketed by calls to
    /// beginAggregate and endAggregate.
    ///
    /// First, each positional component of the aggregate is processed and the
    /// client is notified with a call to acceptPositionalAggregateComponent.
    ///
    /// Next, every keyed component is processed.  This is a two stage affair.
    /// For each key one of the acceptAggregateKey methods is invoked.  Finally,
    /// acceptKeyedAggregateComponent is called with a NodeVector containing
    /// each key along with the associated expression.
    ///
    /// Finally, if the aggregate contains an others clause
    /// acceptAggregateOthers is called.

    /// The start of an aggregate expression is communicated to the client with
    /// a call to beginAggregate().  For each component of the aggregate
    /// acceptAggregateComponent() is called.  The end of the aggregate
    /// expression is voiced with a call to endAggregate().
    //@{

    /// Signals that an aggregate expression is about to be processed.
    ///
    /// \param loc Location of opening paren starting the aggregate.
    virtual void beginAggregate(Location loc) = 0;

    /// Provides a Node describing a positional component of the aggregate.
    virtual void acceptPositionalAggregateComponent(Node component) = 0;

    /// Called to indicate an aggregate key in the form of a range.
    virtual Node acceptAggregateKey(Node lower, Node upper) = 0;

    /// Called to indicate an aggregate key consisting of an expression or
    /// subtype indication.
    virtual Node acceptAggregateKey(Node key) = 0;

    /// \brief Called to indicate an aggregate key consisting of a single
    /// identifier.
    ///
    /// This callback is provided to support selectors of a record aggregate.
    /// Since Comma aggregates cannot be resolved without a type context it is
    /// not reasonable to expect a ParseClient implementation to divine the
    /// meaning of aggregate keys.  This callback allows a client to defer the
    /// processing of aggregate keys which may resolve to a record selector or
    /// to a direct name until enough context is established.
    virtual Node acceptAggregateKey(IdentifierInfo *name, Location loc) = 0;

    /// Provides a vector of keys and associated expression for a keyed
    /// aggregate component.
    ///
    /// \param keys A vector of valid nodes as returned by acceptAggregateKey.
    ///
    /// \param expr Expression denoting the value associated with the given
    /// keys, or a null node if a diamond was parsed.
    ///
    /// \param loc Location of the given expression (for use when \p expr is
    /// null).
    ///
    /// \note The parser will always call this method if it is able to parse the
    /// associated expression.  This means that \p keys may be empty if none of
    /// the keys parsed correctly or the client returned an invalid node for
    /// them all.
    virtual void acceptKeyedAggregateComponent(NodeVector &keys,
                                               Node expr, Location loc) = 0;

    /// Indicates an "others" component.  If this callback is invoked, it is
    /// always the last component processed by the parser.
    ///
    /// \param loc Location of the "others" reserved word.
    ///
    /// \param component An expression denoting the default value for this
    /// aggregate, or a null node if <tt>others => <></tt> was parsed.
    virtual void acceptAggregateOthers(Location loc, Node component) = 0;

    /// Signals that an aggregate expression has completed.
    ///
    /// \return A Node representing the accumulated aggregate expression.
    virtual Node endAggregate() = 0;
    //@}

    /// \name While Statements
    ///
    /// While statements are delimited by calles to beginWhileStmt and
    /// endWhileStmt.  The statements constiuting the body of the loop are
    /// provided via acceptStmt.
    //@{
    virtual Node beginWhileStmt(Location loc, Node condition,
                                IdentifierInfo *tag, Location tagLoc) = 0;
    virtual Node endWhileStmt(Node whileNode) = 0;
    //@}

    /// \name Loop Statements
    ///
    /// Loop statements are delimited by calles to beginLoopStmt and
    /// endLoopStmt.  The statements constiuting the body of the loop are
    /// provided via acceptStmt.
    //@{
    virtual Node beginLoopStmt(Location loc,
                               IdentifierInfo *tag, Location tagLoc) = 0;
    virtual Node endLoopStmt(Node loopNode) = 0;
    //@}

    /// \name For Statement Callbacks.
    ///
    /// A \c for statement is introduced to the client with a call to
    /// beginForStmt().  Each statement in the loop body is then provided to the
    /// client via a call to acceptStmt.  The construct is terminated with a
    /// call to endForStmt().
    //@{

    /// Begins a \c for statement.
    ///
    /// \param loc Location of the \c for reserved word.
    ///
    /// \param iterName Defining identifier for the loop parameter.
    ///
    /// \param iterLoc Location of \p iterName.
    ///
    /// \param control A node representing the subtype definition, range, or
    /// range attribute controlling this loop.  This node is obtained by a call
    /// to acceptDSTDefinition.
    ///
    /// \param isReversed Set to true when the \c reverse reserved word was
    /// present in the loop specification.
    ///
    /// \param tag Identifier naming this loop or null if a tag was not
    /// supplied.
    ///
    /// \param tagLoc Location of \p tag.
    ///
    /// \return A node representing the \c for loop being processed.  The
    /// returned value is supplied to the corresponding endForStmt() call.
    virtual Node beginForStmt(Location loc,
                              IdentifierInfo *iterName, Location iterLoc,
                              Node control, bool isReversed,
                              IdentifierInfo *tag, Location tagLoc) = 0;

    /// Terminates a \c for statement.
    ///
    /// \param forNode The Node returned by the previous call to beginForStmt().
    ///
    /// \return A node representing the completed \c for stmt.
    virtual Node endForStmt(Node forNode) = 0;
    //@}

    /// \name Discrete Subtype Definition Callbacks.
    ///
    /// The following methods inform the client of a discrete subtype
    /// definition.  These methods are invoked when parsing array index
    /// definitions and the control specification of \c for statements.
    //@{

    /// Indicates a discrete subtype definition which was accompanied by a range
    /// constraint.
    ///
    /// \param name The subtype mark of this definition.
    ///
    /// \param lower The lower bound of the range constraint.
    ///
    /// \param upper The upper bound of the range constraint.
    virtual Node acceptDSTDefinition(Node name, Node lower, Node upper) = 0;

    /// Indicates a discrete subtype definition which was specified using a name
    /// which may include a range attribute.  If \p isUnconstrained is true,
    /// then the given node was followed by the "range <>" syntax, and the
    /// parser is in the midst of processing an array type definition.
    virtual Node acceptDSTDefinition(Node nameOrAttribute,
                                     bool isUnconstrained) = 0;

    /// Indicates a discrete subtype definition that was sepcified using only a
    /// range.
    virtual Node acceptDSTDefinition(Node lower, Node upper) = 0;
    //@}

    /// \name Subtype Indication Callbacks.
    ///
    /// These callbacks are invoked to inform the client of a subtype
    /// indication.
    //@{

    /// Called when the subtype indication is not associated with a constraint.
    virtual Node acceptSubtypeIndication(Node prefix) = 0;

    /// Called when the subtype is constrained by a range.
    virtual Node acceptSubtypeIndication(Node prefix, Node lower, Node upper) = 0;

    /// Called when the subtype indication is accompanied by a set of
    /// parenthesized constraints.
    ///
    /// This callback is invoked for subtype indications corresponding to an
    /// array subtype with index constraints (and, eventually, discriminated
    /// records).
    ///
    /// Unfortunately the grammar is ambiguous for this case.  For example,
    /// arrays can have ranges as index constraints whereas discriminated
    /// records accept arbitrary expressions.  For example, the parser cannot
    /// determine which of the arguments in "Foo(X..Y, A => B)" are incorrect.
    /// As a consequence, the client must be prepared to handle a mix of
    /// expressions, keyword selections, and discrete subtype definitions in the
    /// argument vector and decide if the Nodes are compatible with the given
    /// prefix.
    virtual Node acceptSubtypeIndication(Node prefix, NodeVector &arguments) = 0;
    //@}

    /// \brief Indicates an exit statement to the client.
    ///
    /// \param exitLoc Location of the exit reserved word.
    ///
    /// \param tag Identifier this exit statement refers to or null if no taag
    /// was provided.
    ///
    /// \param tagLog Location of \p tag, or an invalid location if \p tag is
    /// null.
    ///
    /// \param condition Expression node describing the exit condition, or a
    /// null node if no condition was provided.
    virtual Node acceptExitStmt(Location exitLoc,
                                IdentifierInfo *tag, Location tagLoc,
                                Node condition) = 0;

    /// \name Block Callbacks.
    ///
    /// Blocks are introduced with a call to beginBlockStmt and terminated with
    /// a call to endBlockStmt.  Each item in the blocks declarative region is
    /// processed via one of the declaration callbacks.  Each statement
    /// associated with the block is registerd via a call to acceptStmt with the
    /// context being the node returned by the initial call to beginBlockStmt.
    //@{

    /// Called when a block statement is about to be parsed.
    virtual Node beginBlockStmt(Location loc, IdentifierInfo *label = 0) = 0;

    /// Once the last statement of a block has been parsed, this method is
    /// called to inform the client that we are leaving the block context
    /// established by the last call to beginBlockStmt.
    virtual void endBlockStmt(Node block) = 0;
    //@}

    /// \name Exception Handler Callbacks.
    ///
    //@{

    /// Begins an exception handler.
    ///
    /// \param loc Location of the \c when reserved word.
    ///
    /// \param choices Set of valid nodes as procudueced by the name callbacks
    /// representing the set of exceptions this handler covers.
    ///
    /// \return A node representing the handler.  This node, if valid, is used
    /// as the context in a call to acceptStmt for each statement constituting
    /// the body of this handler.
    virtual Node beginHandlerStmt(Location loc, NodeVector &choices) = 0;

    /// Completes an exception handler.
    ///
    /// \param context A Node which this handler should be associated with.
    /// This is the result of a call to beginSubroutineDefinition or
    /// beginBlockStmt.
    ///
    /// \param handler The Node returned by a call to beginHandlerStmt.
    virtual void endHandlerStmt(Node context, Node handler) = 0;
    //@}

    /// Called to notify the client of a null statement at the given position.
    virtual Node acceptNullStmt(Location loc) = 0;

    /// Called to notify the client that a statement has been completely
    /// parsed.
    ///
    /// \param context The result of a call to beginBlockStmt or
    /// beginSubroutineDefinition, indicating in which context \p stmt appeared
    /// in.
    ///
    /// \param stmt The actual Node representing the statement (as returned from
    /// accpetIfStmt, acceptAssignmentStmt, etc).
    virtual bool acceptStmt(Node context, Node stmt) = 0;

    virtual bool acceptObjectDeclaration(Location loc, IdentifierInfo *name,
                                         Node type, Node initializer) = 0;

    virtual bool acceptRenamedObjectDeclaration(Location loc,
                                                IdentifierInfo *name,
                                                Node type, Node target) = 0;

    virtual Node acceptProcedureCall(Node name) = 0;

    virtual Node acceptIntegerLiteral(llvm::APInt &value, Location loc) = 0;

    /// Invoked when the parser encounters a string literal.
    ///
    /// \param string A pointer to the first quotation character of the string
    /// literal.  The string is not necessarily null terminated, and is owned by
    /// the parser.  Clients should copy the contents of the string if needed.
    ///
    /// \param len The number of characters in the string.
    ///
    /// \param loc The location of the first quotation character.
    virtual Node acceptStringLiteral(const char *string, unsigned len,
                                     Location loc) = 0;

    /// Invoked when the parser encounters the "null" reserved word in an
    /// expression context.
    virtual Node acceptNullExpr(Location loc) = 0;

    /// Incoked whten the parser encounters an allocator expression.
    ///
    /// \param operand The argument to the allocator.  This is either a name or
    /// result of acceptQualifiedExpr.
    ///
    /// \param loc Location of the "new" reserved word.
    virtual Node acceptAllocatorExpr(Node operand, Location loc) = 0;

    /// Invoked when the parser encounters a qualified expression.
    ///
    /// \param qualifier A node corresponding to the qualified name.
    ///
    /// \param operand The expression to qualify.
    virtual Node acceptQualifiedExpr(Node qualifier, Node operand) = 0;

    /// Notifies the client of an explicit dereference expression.
    ///
    /// \param prefix The name to be dereferenced.
    ///
    /// \param loc Location of the reserved word \c all.
    virtual Node acceptDereference(Node prefix, Location loc) = 0;

    /// Submits a 'use' clause from the given type node.
    virtual bool acceptUseDeclaration(Node usedType) = 0;

    virtual Node acceptIfStmt(Location loc, Node condition,
                              NodeVector &consequents) = 0;

    virtual Node acceptElseStmt(Location loc, Node ifNode,
                                NodeVector &alternates) = 0;

    virtual Node acceptElsifStmt(Location loc, Node ifNode, Node condition,
                                 NodeVector &consequents) = 0;

    virtual Node acceptEmptyReturnStmt(Location loc) = 0;

    virtual Node acceptReturnStmt(Location loc, Node retNode) = 0;

    virtual Node acceptAssignmentStmt(Node target, Node value) = 0;

    /// Called to inform the client of a raise statement.
    ///
    /// \param loc Location of the `raise' reserved word.
    ///
    /// \param exception Node representing the exception to be thrown (as
    /// obtained thru the name callbacks).
    ///
    /// \param message Contains the expression node to be associated with the
    /// `raise', or a null-node if there was no expression.
    virtual Node acceptRaiseStmt(Location loc, Node exception,
                                 Node message) = 0;

    /// Called when a pragma is encountered within a sequence of statements.
    virtual Node acceptPragmaStmt(IdentifierInfo *name, Location loc,
                                  NodeVector &pragmaArgs) = 0;

    /// Called when a pragma Import is encountered.  These pragmas can occur
    /// when processing a list of declarative items.
    ///
    /// \param pragmaLoc The location of the Import identifier.
    ///
    /// \param convention An identifier naming the convention to be used.  Note
    /// that the parser does not know what identifiers name valid conventions.
    ///
    /// \param conventionLoc The location of the \p convention identifier.
    ///
    /// \param entity The identifier naming the entity to import.
    ///
    /// \param entityLoc The location of the \p entity identifier.
    ///
    /// \param externalNameNode An arbitrary expression node.
    virtual void
    acceptPragmaImport(Location pragmaLoc,
                       IdentifierInfo *convention, Location conventionLoc,
                       IdentifierInfo *enity, Location entityLoc,
                       Node externalNameNode) = 0;

    /// \name Enumeration Callbacks.
    ///
    /// Enumerations are processed by first establishing a context with a call
    /// to beginEnumeration.  For each defining enumeration literal, either
    /// acceptEnumerationIdentifier or acceptEnumerationCharacter is called.
    /// Once all elements of the type have been processed, endEnumeration is
    /// called.
    ///
    //@{
    ///
    /// Establishes a context beginning an enumeration type declaration.
    ///
    /// \param name The name of this enumeration type declaration.
    ///
    /// \param loc The location of the enumerations name.
    ///
    virtual void beginEnumeration(IdentifierInfo *name, Location loc) = 0;

    /// Called to introduce an enumeration component which was defined using
    /// identifier syntax.
    ///
    /// \param name The defining identifier for this component.
    ///
    /// \param loc The location of the defining identifier.
    virtual void acceptEnumerationIdentifier(IdentifierInfo *name,
                                             Location loc) = 0;

    /// Called to introduce an enumeration component which was defined using
    /// character syntax.
    ///
    /// \param name The name of the character literal defining this component.
    /// This is always the full name of the literal.  For example, the character
    /// literal for \c X is named using the string \c "'X'" (note that the
    /// quotes are included).
    ///
    /// \param loc The location of the defining character literal.
    ///
    virtual void acceptEnumerationCharacter(IdentifierInfo *name,
                                            Location loc) = 0;

    /// Called when all of the enumeration literals have been processed, thus
    /// completing the definition of the enumeration.
    virtual void endEnumeration() = 0;
    //@}

    /// Called to process integer type declarations.
    ///
    /// For example, given a definition of the form <tt>type T is range
    /// X..Y;</tt>, this callback is invoked with \p name set to the identifier
    /// \c T, \p loc set to the location of \p name, \p low set to the
    /// expression \c X, and \p high set to the expression \c Y.
    virtual void acceptIntegerTypeDecl(IdentifierInfo *name, Location loc,
                                       Node low, Node high) = 0;

    /// Called to process modular integer type declarations.
    ///
    /// Given a definition of the form <tt>type T is mod X</tt> this callback is
    /// invoked with \p name set to the identifier \c T, \p loc set to the
    /// location of \p name, and \p modulus set to an expression node
    /// representing \c X.
    virtual void acceptModularTypeDecl(IdentifierInfo *name, Location loc,
                                       Node modulus) = 0;

    /// Called to process a range constrained discrete subtype declaration.
    ///
    /// For example, given a declaration of the form <tt>subtype S is T range
    /// X..Y;</tt>, this callback is invoked with \p name set to the identifier
    /// \c S, \p loc set to the location of \p name, and \p subtype set to the
    /// subtype indication \c T, and the last two arguments (\p low and \p high)
    /// represent the lower and upper bounds of the range constraint.
    virtual void acceptRangedSubtypeDecl(IdentifierInfo *name, Location loc,
                                         Node subtype,
                                         Node low, Node high) = 0;

    /// Called to process an unconstrained subtype declaration.
    ///
    /// For example, given a declaration of the form <tt>subtype S is T;</tt>,
    /// this callback is invoked with \p name set to the identifier \c S, \p loc
    /// set to the location of \p name, and \p subtype set to the subtype
    /// indication \c T.
    virtual void acceptSubtypeDecl(IdentifierInfo *name, Location loc,
                                   Node subtype) = 0;

    /// Called to notify the client of an incomplete type declaration.
    ///
    /// This callback is invoked when processing type declarations of the form
    /// <tt>type Foo;<\tt>.
    virtual void acceptIncompleteTypeDecl(IdentifierInfo *name,
                                          Location loc) = 0;

    /// Called to notify the client of an access type declaration.
    virtual void acceptAccessTypeDecl(IdentifierInfo *name, Location loc,
                                      Node subtype) = 0;

    /// \brief Communicates an array type declaration.
    ///
    /// \param name The name of this array type declaration.
    ///
    /// \param loc the location of \p name.
    ///
    /// \param indices A set of nodes defining the index specification for this
    /// array, each being the result of a call to acceptDSTDefinition.
    ///
    /// \param component The component type of the array declaration.
    virtual void acceptArrayDecl(IdentifierInfo *name, Location loc,
                                 NodeVector indices, Node component) = 0;

    /// \name Record Type Declaration Callbacks.
    ///
    /// Record type declarations are braketed between calls to beginRecord and
    /// endRecord.  For each component of the record type a call to
    /// acceptRecordComponent is called.
    //@{

    /// Begins the definition of a record type.
    ///
    /// \param name The defining identifier for this record.
    ///
    /// \param loc The location of the defining identifier.
    virtual void beginRecord(IdentifierInfo *name, Location loc) = 0;

    /// Called to notify the client of a record component.
    ///
    /// \param name The defining identifier for this record component.
    ///
    /// \param loc The location of the components defining identifier.
    ///
    /// \param type A node representing the type of this identifier.
    virtual void acceptRecordComponent(IdentifierInfo *name, Location loc,
                                       Node type) = 0;

    /// Completes the definition of a record type.
    virtual void endRecord() = 0;
    //@}

    /// Informs the client of a private type declaration.
    virtual void acceptPrivateTypeDecl(IdentifierInfo *name, Location loc,
                                       unsigned typeTag) = 0;

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

//===----------------------------------------------------------------------===//
// Inline methods.

inline void Node::dispose()
{
    assert(state->rc != 0);
    if (--state->rc == 0) {
        if (isOwning())
            state->client.getPointer()->deleteNode(*this);
        delete state;
    }
}

inline Node &Node::operator=(const Node &node)
{
    if (state != node.state) {
        ++node.state->rc;
        dispose();
        state = node.state;
    }
    return *this;
}

inline void Node::release()
{
    unsigned prop = state->client.getInt();
    state->client.setInt(prop | NodeState::Released);
}

inline bool Node::isOwning()
{
    return !(state->client.getInt() & NodeState::Released);
}

inline void Node::markInvalid()
{
    unsigned prop = state->client.getInt();
    state->client.setInt(prop | NodeState::Invalid);
}

inline void NodeVector::release()
{
    for (iterator iter = begin(); iter != end(); ++iter)
        iter->release();
}

} // End comma namespace.

#endif
