//===-- ast/Stmt.h -------------------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008-2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#ifndef COMMA_AST_STMT_HDR_GUARD
#define COMMA_AST_STMT_HDR_GUARD

#include "comma/ast/AstBase.h"
#include "comma/ast/DeclRegion.h"
#include "comma/ast/SubroutineCall.h"

namespace comma {

class Pragma;

//===----------------------------------------------------------------------===//
// Stmt
class Stmt : public Ast {

protected:
    Stmt(AstKind kind) : Ast(kind) { }

public:
    static bool classof(const Stmt *node) { return true; }
    static bool classof(const Ast *node) {
        return node->denotesStmt();
    }
};

//===----------------------------------------------------------------------===//
// StmtSequence
//
// Represents a sequence of statements.
class StmtSequence : public Stmt {

    llvm::SmallVector<Stmt*, 16> statements;

protected:
    StmtSequence(AstKind kind) : Stmt(kind) { }

public:
    StmtSequence() : Stmt(AST_StmtSequence) { }

    /// Construct a statement sequence given a pair of Stmt producing iterators.
    template <class Iter>
    StmtSequence(Iter I, Iter E)
        : Stmt(AST_StmtSequence),
          statements(I, E) { }

    void addStmt(Stmt *stmt) { statements.push_back(stmt); }

    template <class Iter>
    void addStmts(Iter I, Iter E) {
        for ( ; I != E; ++I)
            statements.push_back(*I);
    }

    /// Returns the number of statements contained in this sequence.
    unsigned size() const { return statements.size(); }

    typedef llvm::SmallVector<Stmt*, 16>::iterator StmtIter;
    StmtIter beginStatements() { return statements.begin(); }
    StmtIter endStatements()   { return statements.end(); }

    typedef llvm::SmallVector<Stmt*, 16>::const_iterator ConstStmtIter;
    ConstStmtIter beginStatements() const { return statements.begin(); }
    ConstStmtIter endStatements()   const { return statements.end(); }

    static bool classof(const StmtSequence *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_StmtSequence ||
            node->getKind() == AST_BlockStmt;
    }
};

//===----------------------------------------------------------------------===//
// BlockStmt
//
// Represents a block statement consisting of an optional identifier, a possibly
// empty declarative region, and a sequence of statements constituting the body.
class BlockStmt : public StmtSequence, public DeclRegion {

public:
    BlockStmt(Location        loc,
              DeclRegion     *parent,
              IdentifierInfo *label = 0)
        : StmtSequence(AST_BlockStmt),
          DeclRegion(AST_BlockStmt, parent),
          location(loc),
          label(label) { }

    // Returns true if this block has an associated label.
    bool hasLabel() const { return label != 0; }

    // Returns the label associated with this block, or 0 if there is no such
    // label.
    IdentifierInfo *getLabel() { return label; }

    Location getLocation() { return location; }

    static bool classof(const BlockStmt *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_BlockStmt;
    }

private:
    Location        location;
    IdentifierInfo *label;
};

//===----------------------------------------------------------------------===//
// ProcedureCallStmt
//
// Representation of a procedure call statement.
class ProcedureCallStmt : public Stmt, public SubroutineCall {

public:
    ProcedureCallStmt(SubroutineRef *ref,
                      Expr **positionalArgs, unsigned numPositional,
                      KeywordSelector **keyedArgs, unsigned numKeys);

    /// Returns the location of this procedure call.
    Location getLocation() const { return location; }

    //@{
    /// Returns the procedure declaration underlying this call.
    const ProcedureDecl *getConnective() const {
        return llvm::cast<ProcedureDecl>(SubroutineCall::getConnective());
    }
    ProcedureDecl *getConnective() {
        return llvm::cast<ProcedureDecl>(SubroutineCall::getConnective());
    }
    //@}

    // Support isa and dyn_cast.
    static bool classof(const ProcedureCallStmt *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_ProcedureCallStmt;
    }

private:
    Location location;
};

//===----------------------------------------------------------------------===//
// ReturnStmt.
class ReturnStmt : public Stmt {

    Expr    *returnExpr;
    Location location;

public:
    ReturnStmt(Location loc, Expr *expr = 0)
        : Stmt(AST_ReturnStmt), returnExpr(expr), location(loc) { }

    ~ReturnStmt();

    bool hasReturnExpr() const { return returnExpr != 0; }

    const Expr *getReturnExpr() const { return returnExpr; }
    Expr *getReturnExpr() { return returnExpr; }

    Location getLocation() const { return location; }

    static bool classof(const ReturnStmt *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_ReturnStmt;
    }
};

//===----------------------------------------------------------------------===//
// AssignmentStmt
class AssignmentStmt : public Stmt {

    Expr *target;
    Expr *value;

public:
    AssignmentStmt(Expr *target, Expr *value)
        : Stmt(AST_AssignmentStmt), target(target), value(value) { }

    Expr *getTarget() { return target; }
    const Expr *getTarget() const { return target; }

    Expr *getAssignedExpr() { return value; }
    const Expr *getAssignedExpr() const { return value; }

    static bool classof(const AssignmentStmt *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_AssignmentStmt;
    }
};

//===----------------------------------------------------------------------===//
// IfStmt
class IfStmt : public Stmt {

public:
    // IfStmt's are always constructed with a condition and a consequent.  If
    // the statement contains "elsif" components, one must call addElsif for
    // each component.  Similarly, one must call setAlternate to define the
    // "else" component.
    IfStmt(Location loc, Expr *condition, StmtSequence *consequent)
        : Stmt(AST_IfStmt),
          ifLocation(loc),
          elseLocation(0),
          condition(condition),
          consequent(consequent),
          alternate(0) { }

    // Returns the predicate expression controlling this IfStmt.
    Expr *getCondition() { return condition; }
    const Expr *getCondition() const { return condition; }

    // Returns the statement associated with the "then" branch of this IfStmt.
    StmtSequence *getConsequent() { return consequent; }
    const StmtSequence *getConsequent() const { return consequent; }

    // Sets the statement associated with the "else" branch of this IfStmt.
    void setAlternate(Location loc, StmtSequence *stmt) {
        assert(alternate == 0 &&  "Cannot reset IfStmt alternate!");
        elseLocation = loc;
        alternate    = stmt;
    }

    // Returns true if this IfStmt has been supplied with an "else" clause.
    bool hasAlternate() const { return alternate != 0; }

    // Returns the statement associated with the "else" clause, or 0 if no such
    // component exists.
    StmtSequence *getAlternate() { return alternate; }
    const StmtSequence *getAlternate() const { return alternate; }

    // The following class is used to represent "elsif" components of a
    // conditional.
    class Elsif {

    public:
        Location getLocation() const { return location; }

        Expr *getCondition() { return condition; }
        const Expr *getCondition() const { return condition; }

        StmtSequence *getConsequent() { return consequent; }
        const StmtSequence *getConsequent() const { return consequent; }

    private:
        Elsif(Location loc, Expr *cond, StmtSequence *stmt)
            : location(loc), condition(cond), consequent(stmt) { }

        friend class IfStmt;

        Location      location;
        Expr         *condition;
        StmtSequence *consequent;
    };

private:
    // The type used to store Elsif components.
    typedef llvm::SmallVector<Elsif, 2> ElsifVector;

public:
    typedef ElsifVector::iterator       iterator;
    typedef ElsifVector::const_iterator const_iterator;

    iterator beginElsif() { return elsifs.begin(); }
    iterator endElsif()   { return elsifs.end(); }

    const_iterator beginElsif() const { return elsifs.begin(); }
    const_iterator endElsif()   const { return elsifs.end(); }

    // Adds an "elsif" branch to this IfStmt.  The order in which this function
    // is called determines the order of the elsif branches.
    void addElsif(Location loc, Expr *condition, StmtSequence *consequent) {
        elsifs.push_back(Elsif(loc, condition, consequent));
    }

    // Returns true if this if statement contains elsif clauses.
    bool hasElsif() const { return !elsifs.empty(); }

    // Returns the location of the "if" token.
    Location getIfLocation() const { return ifLocation; }

    // Returns the location of the "else" token if an alternate branch exists.
    Location getElseLocation() const { return elseLocation; }

    static bool classof(const IfStmt *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_IfStmt;
    }

private:
    Location      ifLocation;
    Location      elseLocation;
    Expr         *condition;
    StmtSequence *consequent;
    StmtSequence *alternate;
    ElsifVector   elsifs;
};

//===----------------------------------------------------------------------===//
// WhileStmt
//
// Ast nodes representing the 'while' loop construct.
class WhileStmt : public Stmt {

public:
    WhileStmt(Location loc, Expr *condition, StmtSequence *body)
        : Stmt(AST_WhileStmt),
          location(loc),
          condition(condition),
          body(body) { }

    // Returns the condition expression controlling this loop.
    Expr *getCondition() { return condition; }
    const Expr *getCondition() const { return condition; }

    // Returns the body of this loop.
    StmtSequence *getBody() { return body; }
    const StmtSequence *getBody() const { return body; }

    // Returns the location of the 'while' reserved word starting this loop.
    Location getLocation() { return location; }

    static bool classof(const WhileStmt *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_WhileStmt;
    }

private:
    Location location;
    Expr *condition;
    StmtSequence *body;
};

//===----------------------------------------------------------------------===//
// ForStmt
//
/// This node represents the "for" loop iteration scheme.
class ForStmt : public Stmt {

    /// For statements are controlled by discrete subtype definitions which have
    /// several ast representations .  The following enumeration encodes the
    /// type of the control, which is munged into the low order bits of a
    /// generic Ast pointer.
    enum ControlKind {
        Range_Attribute_Control, ///< Range attribute.
        Range_Control,           ///< Simple range.
        Type_Control             ///< Discrete type.
    };

    /// The type used to manage the control node for this loop.
    typedef llvm::PointerIntPair<Ast*, 2, ControlKind> TaggedControl;

public:
    /// \brief Constructs a for-loop statement over the given declaration and
    /// control node.
    ///
    /// The control node for this loop must be either a RangeAttrib, a Range, or
    /// a DiscreteType.
    ForStmt(Location loc, LoopDecl *iterationDecl, Ast *control);

    //@{
    /// Returns the LoopDecl corresponding to the iteration value of this loop.
    const LoopDecl *getLoopDecl() const { return iterationDecl; }
    LoopDecl *getLoopDecl() { return iterationDecl; }
    //@}

    //@{
    /// Returns the subtype, range, or range attribute controlling this loop.
    const Ast *getControl() const;
    Ast *getControl();
    //@}

    //@{
    /// Returns the range attribute this loop was specified over, or null if the
    /// controlling subtype is not a range attribute.
    const RangeAttrib *getAttribControl() const;
    RangeAttrib *getAttribControl();
    //@}

    /// Returns true if this loop is controlled by a range attribute.
    bool isAttribControlled() const {
        return control.getInt() == Range_Attribute_Control;
    }

    //@{
    /// Returns the controlling range of this loop, or null if the controlling
    /// subtype is not a range.
    const Range *getRangeControl() const {
        if (isRangeControlled())
            return llvm::cast<Range>(control.getPointer());
        return 0;
    }
    Range *getRangeControl() {
        if (isRangeControlled())
            return llvm::cast<Range>(control.getPointer());
        return 0;
    }
    //@}

    /// Returns true if this loop is controlled by a range.
    bool isRangeControlled() const {
        return control.getInt() == Range_Control;
    }

    //@{
    /// Returns the controlling subtype of this looop, or null if the
    /// control is not a subtype.
    const DiscreteType *getTypeControl() const {
        if (isTypeControlled())
            return llvm::cast<DiscreteType>(control.getPointer());
        return 0;
    }
    DiscreteType *getTypeControl() {
        if (isTypeControlled())
            return llvm::cast<DiscreteType>(control.getPointer());
        return 0;
    }
    //@}

    /// Returns true if this loop is controlled by a discrete type.
    bool isTypeControlled() const {
        return control.getInt() == Type_Control;
    }

    /// Returns true if the controlling scheme is reversed.
    bool isReversed() const { return bits == 1; }

    /// Marks that this loop is reversed.
    void markAsReversed() { bits = 1; }

    /// Returns the location of the 'for' reserved word.
    Location getLocation() { return location; }

    //@{
    /// Retururns the StmtSequence forming the body of this loop.
    ///
    /// Initially, this sequence is empty and must be populated via explicit
    /// calls to StmtSequence::addStmt().
    const StmtSequence *getBody() const { return &body; }
    StmtSequence *getBody() { return &body; }
    //@}

    // Support isa/dyn_cast.
    static bool classof(const ForStmt *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_ForStmt;
    }

private:
    Location location;
    LoopDecl *iterationDecl;
    TaggedControl control;
    StmtSequence body;
};

//===----------------------------------------------------------------------===//
// LoopStmt
//
/// This class represents the simple "loop" statement.
class LoopStmt : public Stmt {

public:
    LoopStmt(Location loc, StmtSequence *body)
        : Stmt(AST_LoopStmt),
          location(loc),
          body(body) { }

    //@{
    /// Returns the body of this loop.
    const StmtSequence *getBody() const { return body; }
    StmtSequence *getBody() { return body; }
    //@}

    /// Returns the location of the 'loop' reserved word.
    Location getLocation() { return location; }

    // Support isa/dyn_cast.
    static bool classof(const LoopStmt *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_LoopStmt;
    }

private:
    Location location;
    StmtSequence *body;
};

//===----------------------------------------------------------------------===//
// PragmaStmt
//
// This is a simple Stmt node which wraps a pragma so that it can appear within
// a sequence of statements.
class PragmaStmt : public Stmt {

public:
    PragmaStmt(Pragma *pragma)
        : Stmt(AST_PragmaStmt), pragma(pragma) { }

    const Pragma *getPragma() const { return pragma; }
    Pragma *getPragma() { return pragma; }

private:
    Pragma *pragma;
};

} // End comma namespace.

#endif
