//===-- ast/Type.h -------------------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008-2010, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#ifndef COMMA_AST_TYPE_HDR_GUARD
#define COMMA_AST_TYPE_HDR_GUARD

#include "comma/ast/AstBase.h"
#include "comma/ast/Range.h"
#include "comma/basic/ParameterModes.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/Casting.h"

namespace comma {

//===----------------------------------------------------------------------===//
// Type

class Type : public Ast {

public:
    virtual ~Type() { }

    /// The following enumeration lists the "interesting" language-defined
    /// classes.
    enum Classification {
        CLASS_Scalar,
        CLASS_Discrete,
        CLASS_Enum,
        CLASS_Integer,
        CLASS_Composite,
        CLASS_Array,
        CLASS_String,
        CLASS_Access,
        CLASS_Record
    };

    /// Returns true if this type is a member of the given classification.
    bool memberOf(Classification ID) const;

    /// Returns true if this type denotes a scalar type.
    bool isScalarType() const;

    /// Returns true if this type denotes a discrete type.
    bool isDiscreteType() const;

    /// Returns true if this type denotes an integer type.
    bool isIntegerType() const;

    /// Returns true if this type denotes a numeric type (either an integer or
    /// real type).
    bool isNumericType() const;

    /// Returns true if this type denotes an enumeration type.
    bool isEnumType() const;

    /// Returns true if this type denotes a composite type.
    bool isCompositeType() const;

    /// Returns true if this type denotes an array type.
    bool isArrayType() const;

    /// Returns true if this type denotes a record type.
    bool isRecordType() const;

    /// Returns true if this type denotes a string type.
    bool isStringType() const;

    /// Returns true if this type denotes an access type.
    bool isAccessType() const;

    /// Returns true if this type denotes a fat access type.
    bool isFatAccessType() const;

    /// Returns true if this type denotes a thin access type.
    bool isThinAccessType() const;

    /// Returns true if this is a universal type.
    bool isUniversalType() const;

    /// Returns true if this is the universal integer type.
    bool isUniversalIntegerType() const;

    /// Returns true if this is the universal access type.
    bool isUniversalAccessType() const;

    /// Returns true if this is the universal fixed type.
    bool isUniversalFixedType() const;

    /// Returns true if this is the universal real type.
    bool isUniversalRealType() const;

    /// Returns true if this is a universal type which contains (or covers) the
    /// given type.
    bool isUniversalTypeOf(const Type *type) const;

    /// \brief Returns true if this is an indenfinite type.
    ///
    /// An indefinite type is a type whose size is unknown at compile time.
    /// Currently, the only example of an indefinite type in Comma is an
    /// unconstrained array type.
    bool isIndefiniteType() const;

    /// \brief Returns true if this is a definite type.
    ///
    /// This is the inverse of isIndefiniteType.  The size of such types are
    /// statically known.
    bool isDefiniteType() const { return !isIndefiniteType(); }

    /// Returns true if this type is a subtype of the given type.
    ///
    /// All types are considered to be subtypes of themselves.
    bool isSubtypeOf(const Type *type) const;

    // Support isa/dyn_cast.
    static bool classof(const Type *node) { return true; }
    static bool classof(const Ast *node) {
        return node->denotesType();
    }

protected:
    Type(AstKind kind) : Ast(kind) {
        // Types are never directly deletable -- they are always owned by a
        // containing node.
        deletable = false;
        assert(this->denotesType());
    }

private:
    Type(const Type &);         // Do not implement.
};

//===----------------------------------------------------------------------===//
// SubroutineType
class SubroutineType : public Type {

public:
    virtual ~SubroutineType() { delete[] argumentTypes; }

    /// Returns the number of arguments accepted by this type.
    unsigned getArity() const { return numArguments; }

    /// Returns the type of the i'th parameter.
    Type *getArgType(unsigned i) const { return argumentTypes[i]; }

    /// Iterators over the argument types.
    typedef Type **arg_type_iterator;
    arg_type_iterator begin() const { return argumentTypes; }
    arg_type_iterator end() const {
        return argumentTypes + numArguments; }

    /// Compares the profiles of two subroutine types for equality.
    ///
    /// Two subroutine profiles are considered equal if:
    ///
    ///   - both denote procedures or functions;
    ///
    ///   - both have the same number of arguments;
    ///
    ///   - each corresponding pair of argument types are equal or both are
    ///     derived from a common root;
    ///
    ///   - the return types (if present) are equal or derive from a common
    ///     root.
    static bool compareProfiles(const SubroutineType *X,
                                const SubroutineType *Y);

    // Support isa and dyn_cast.
    static bool classof(const SubroutineType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->denotesSubroutineType();
    }

protected:
    SubroutineType(AstKind kind, Type **argTypes, unsigned numArgs);

    Type **argumentTypes;
    unsigned numArguments;
};

//===----------------------------------------------------------------------===//
// FunctionType
class FunctionType : public SubroutineType, public llvm::FoldingSetNode {

public:
    /// Returns the result type of this function.
    Type *getReturnType() const { return returnType; }

    /// Profile implementation for use by llvm::FoldingSet.
    void Profile(llvm::FoldingSetNodeID &ID) {
        Profile(ID, argumentTypes, numArguments, returnType);
    }

    // Support isa and dyn_cast.
    static bool classof(const FunctionType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_FunctionType;
    }

private:
    Type *returnType;

    /// Function types are constructed thru an AstResource.
    friend class AstResource;

    FunctionType(Type **argTypes, unsigned numArgs, Type *returnType)
        : SubroutineType(AST_FunctionType, argTypes, numArgs),
          returnType(returnType) { }

    /// Profiler used by AstResource to unique function type nodes.
    static void Profile(llvm::FoldingSetNodeID &ID,
                        Type **argTypes, unsigned numArgs,
                        Type *returnType) {
        for (unsigned i = 0; i < numArgs; ++i)
            ID.AddPointer(argTypes[i]);
        ID.AddPointer(returnType);
    }
};

//===----------------------------------------------------------------------===//
// ProcedureType
class ProcedureType : public SubroutineType, public llvm::FoldingSetNode {

public:
    /// Profile implementation for use by llvm::FoldingSet.
    void Profile(llvm::FoldingSetNodeID &ID) {
        Profile(ID, argumentTypes, numArguments);
    }

    // Support isa and dyn_cast.
    static bool classof(const ProcedureType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_ProcedureType;
    }

private:
    /// ProcedureTypes are constructed thru AstResource.
    friend class AstResource;

    ProcedureType(Type **argTypes, unsigned numArgs)
        : SubroutineType(AST_ProcedureType, argTypes, numArgs) { }

    /// Profiler used by AstResource to unique procedure type nodes.
    static void Profile(llvm::FoldingSetNodeID &ID,
                        Type **argTypes, unsigned numArgs) {
        if (numArgs)
            for (unsigned i = 0; i < numArgs; ++i)
                ID.AddPointer(argTypes[i]);
        else
            ID.AddPointer(0);
    }
};

//===----------------------------------------------------------------------===//
// UniversalType
//
/// This type represents the various universal types such as universal_integer,
/// universal_access, etc.
class UniversalType : public Type {

public:

    /// \name Static Constructors.
    //@{
    static UniversalType *getUniversalInteger() {
        if (universal_integer)
            return universal_integer;
        universal_integer = new UniversalType();
        return universal_integer;
    }

    static UniversalType *getUniversalAccess() {
        if (universal_access)
            return universal_access;
        universal_access = new UniversalType();
        return universal_access;
    }

    static UniversalType *getUniversalFixed() {
        if (universal_fixed)
            return universal_fixed;
        universal_fixed = new UniversalType();
        return universal_fixed;
    }

    static UniversalType *getUniversalReal() {
        if (universal_real)
            return universal_real;
        universal_real = new UniversalType();
        return universal_real;
    }
    //@}

    /// \name Predicates.
    //@{
    bool isUniversalIntegerType() const { return this == universal_integer; }
    bool isUniversalAccessType()  const { return this == universal_access;  }
    bool isUniversalFixedType()   const { return this == universal_fixed;   }
    bool isUniversalRealType()    const { return this == universal_real;    }
    //@}

    /// Returns the classification this universal type represents.
    Classification getClassification() const {
        // FIXME: Support all classifications.
        if (isUniversalIntegerType())
            return CLASS_Integer;
        assert(isUniversalAccessType() && "Unexpected universal type!");
        return CLASS_Access;
    }

    // Support isa/dyn_cast.
    static bool classof(const UniversalType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_UniversalType;
    }

private:
    UniversalType() : Type(AST_UniversalType) { }

    /// The various universal types.  Initialized on the first call to the get
    /// routines.
    static UniversalType *universal_integer;
    static UniversalType *universal_access;
    static UniversalType *universal_fixed;
    static UniversalType *universal_real;
};

//===----------------------------------------------------------------------===//
// PrimaryType
//
/// The PrimaryType class forms the principle root of the type hierarchy.  Most
/// type nodes inherit from PrimaryType, with the notable exception of
/// SubroutineType.
class PrimaryType : public Type {

public:
    /// Returns true if this node denotes a subtype.
    bool isSubtype() const { return typeChain.getInt(); }

    /// Returns true if this node denotes a root type.
    bool isRootType() const { return !isSubtype(); }

    //@{
    /// Returns the root type of this type.  If this is a root type, returns a
    /// pointer to this, otherwise the type of this subtype is returned.
    const PrimaryType *getRootType() const {
        return const_cast<PrimaryType*>(this)->getRootType();
    }
    PrimaryType *getRootType() {
        PrimaryType *cursor = this;
        while (cursor->isSubtype())
            cursor = cursor->typeChain.getPointer();
        return cursor;
    }
    //@}

    /// Returns true if this is a derived type.
    bool isDerivedType() const {
        const PrimaryType *root = getRootType();
        return root->typeChain.getPointer() != 0;
    }

    //@{
    /// \brief Returns the parent type of this type, or null if isDerivedType()
    /// returns false.
    PrimaryType *getParentType() {
        PrimaryType *root = getRootType();
        return root->typeChain.getPointer();
    }
    const PrimaryType *getParentType() const {
        const PrimaryType *root = getRootType();
        return root->typeChain.getPointer();
    }
    //@}

    //@{
    /// \brief Returns the first ancestor type of this type, or null if this is
    /// a root type without a parent.
    const PrimaryType *getAncestorType() const {
        return typeChain.getPointer();
    }
    PrimaryType *getAncestorType() { return typeChain.getPointer(); }
    //@}

    /// Returns true if this type is constrained.
    ///
    /// \note Default implementation returns false.
    virtual bool isConstrained() const { return false; }

    /// Returns true if this type is unconstrained.
    bool isUnconstrained() const { return !isConstrained(); }

    // Support isa/dyn_cast.
    static bool classof(const PrimaryType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->denotesPrimaryType();
    }

protected:
    /// Protected constructor for primary types.
    ///
    /// \param kind The concrete kind tag for this node.
    ///
    /// \param rootOrParent If this is to represent a root type, then this
    /// argument is a pointer to the parent type or null.  If this is to
    /// represent a subtype, then rootOrParent should point to the type of this
    /// subtype.
    ///
    /// \param subtype When true, the type under construction is a subtype.
    /// When false, the type is a root type.
    PrimaryType(AstKind kind, PrimaryType *rootOrParent, bool subtype)
        : Type(kind) {
        assert(this->denotesPrimaryType());
        typeChain.setPointer(rootOrParent);
        typeChain.setInt(subtype);
    }

private:
    /// The following field encapsulates a bit which marks this node as either a
    /// subtype or root type, and a pointer to this types ancestor (if any).
    ///
    /// When this type denotes a subtype, the following field contains a link to
    /// the root type (the type of the subtype) or the immediate ancestor of the
    /// subtype.  Otherwise, this is a root type and typeChain points to the
    /// parent type or null.
    llvm::PointerIntPair<PrimaryType*, 1, bool> typeChain;
};

//===----------------------------------------------------------------------===//
// IncompleteType
//
/// Incomplete types are associated with a particular IncompleteTypeDecl.  They
/// represent an incomplete view of some specific type.
class IncompleteType : public PrimaryType {

public:
    /// Returns the defining identifier of this type;
    IdentifierInfo *getIdInfo() const;

    /// Returns the defining identifier of this type as a C-string.
    const char *getString() const { return getIdInfo()->getString(); }

    //@{
    /// Returns the incomplete type declaration that introduced this type.
    const IncompleteTypeDecl *getDefiningDecl() const {
        return const_cast<IncompleteType*>(this)->getDefiningDecl();
    }
    IncompleteTypeDecl *getDefiningDecl();
    //@}

    /// Returns true if this type as a completion.
    bool hasCompletion() const;

    //@{
    /// Returns the underlying complete type.
    const PrimaryType *getCompleteType() const {
        return const_cast<IncompleteType*>(this)->getCompleteType();
    }
    PrimaryType *getCompleteType();
    //@}

    //@{
    /// Specialize PrimaryType::getRootType().
    IncompleteType *getRootType() {
        return llvm::cast<IncompleteType>(PrimaryType::getRootType());
    }
    const IncompleteType *getRootType() const {
        return llvm::cast<IncompleteType>(PrimaryType::getRootType());
    }
    //@}

    // Support isa/dyn_cast.
    static bool classof(const IncompleteType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_IncompleteType;
    }

private:
    /// Creates an IncompleteType corresponding to the given IncompleteType
    /// declaration.
    IncompleteType(IncompleteTypeDecl *decl)
        : PrimaryType(AST_IncompleteType, 0, false),
          definingDecl(decl) { }

    /// Creates a subtype of the given incomplete type.
    IncompleteType(IncompleteType *rootType, IdentifierInfo *name)
        : PrimaryType(AST_IncompleteType, rootType, true),
          definingDecl(name) { }

    /// Incomplete types are allocated and managed by AstResource.
    friend class AstResource;

    /// When a root incomplete type is constructed this union contains a pointer
    /// to the corresponding incomplete type declaration.  For subtypes, this is
    /// a pointer to the identifier info naming the subtype.
    llvm::PointerUnion<IncompleteTypeDecl *, IdentifierInfo *> definingDecl;
};

//===----------------------------------------------------------------------===//
// DiscreteType
//
/// The DiscreteType class forms a common base for integer and enumeration
/// types.
class DiscreteType : public PrimaryType {

public:
    /// Returns the defining identifier for this type.
    virtual IdentifierInfo *getIdInfo() const = 0;

    /// Returns the upper limit for this type.
    ///
    /// The upper limit is the greatest value which can be represented by the
    /// underlying root type.  Note that this is not a bound as expressed via a
    /// subtype constraint.
    virtual void getUpperLimit(llvm::APInt &res) const = 0;

    /// Returns the lower limit for this type.
    ///
    /// The lower limit is the smallest value which can be represented by the
    /// underlying root type.  Note that this is not a bound as expressed via a
    /// subtype constraint.
    virtual void getLowerLimit(llvm::APInt &res) const = 0;

    /// Returns the number of bits needed to represent this type.
    ///
    /// The value returned by this method is equivalent to Size attribute.  The
    /// number returned specifies the minimum number of bits needed to represent
    /// values of this type, as opposed to the number of bits used to represent
    /// values of this type at runtime.
    virtual uint64_t getSize() const = 0;

    /// Returns the number of elements representable by this type.
    ///
    /// This method may only be called on a statically constrained or
    /// unconstrained type, else an assertion will fire.
    uint64_t length() const;

    /// The following enumeration is used to report the result of containment
    /// predicates.  These values define a ternary logic.
    enum ContainmentResult {
        Is_Contained,
        Not_Contained,
        Maybe_Contained
    };

    /// Returns a ContainmentResult indicating if this DiscreteType contains
    /// another.
    ///
    /// This type and the target type must be of the same category.  That is,
    /// both must be integer, enumeration, or (when implemented) modular types.
    ///
    /// Containment is with respect to the bounds on the types.  If a type is
    /// constrained, then the constraint is used for the bounds, otherwise the
    /// representational limits of the root type are used.
    ///
    /// If this type is constrained to a null range it can never contain the
    /// target, including other null types (with the only exception being that
    /// all types trivially contain themselves).  If this type is not
    /// constrained to a null range, then it always contains a target type that
    /// is.
    ///
    /// If this type has a non-static constraint, this method always returns
    /// Maby_Contained.  If the target has a non-static constraint but the
    /// bounds for this type are known, containment is known only if this type
    /// contains the root type of the target.
    ContainmentResult contains(const DiscreteType *target) const;

    /// Returns a ContainmentResult for the given integer value.
    ContainmentResult contains(const llvm::APInt &value) const;

    /// Returns true if this denotes a signed discrete type.
    ///
    /// Integers are signed while modular and enumeration types are not.
    bool isSigned() const;

    //@{
    /// Specialization of PrimaryType::getRootType().
    const DiscreteType *getRootType() const {
        return llvm::cast<DiscreteType>(PrimaryType::getRootType());
    }
    DiscreteType *getRootType() {
        return llvm::cast<DiscreteType>(PrimaryType::getRootType());
    }
    //@}

    //@{
    /// Returns the constraint associated with this DiscreteType or null if this
    /// type is unconstrained.
    virtual Range *getConstraint() = 0;
    virtual const Range *getConstraint() const = 0;
    //@}

    /// Returns true if this type is constrained and the constraints are static.
    bool isStaticallyConstrained() const {
        if (const Range *range = getConstraint())
            return range->isStatic();
        return false;
    }

    /// Returns true if this type is constrained and at least one component of
    /// the constraint is dynamic.
    bool isDynamicallyConstrained() const {
        if (const Range *range = getConstraint())
            return !range->isStatic();
        return false;
    }

    /// Returns the declaration corresponding to the Pos attribute for this
    /// type.
    virtual PosAD *getPosAttribute() = 0;

    /// Returns the declaration corresponding to the Val attribute for this
    /// type.
    virtual ValAD *getValAttribute() = 0;

    //@{
    /// Returns the declaration defining this discrete type.
    ///
    /// FIXME: This interface is awkward.  We cannot use covariant return types
    /// to specialize these methods since we cannot directly depend on Decl.h.
    virtual const TypeDecl *getDefiningDecl() const = 0;
    virtual TypeDecl *getDefiningDecl() = 0;
    //@}

    // Support isa/dyn_cast.
    static bool classof(const DiscreteType *node) { return true; }
    static bool classof(const Ast *node) {
        return denotesDiscreteType(node->getKind());
    }

protected:
    DiscreteType(AstKind kind, DiscreteType *rootOrParent, bool subtype)
        : PrimaryType(kind, rootOrParent, subtype) {
        assert(denotesDiscreteType(kind));
    }

    // Convinience utility for subclasses.  Returns the number of bits that
    // should be used for the size of the type, given the minimal number of bits
    // needed to represent the entity.
    static unsigned getPreferredSize(uint64_t bits);

private:
    static bool denotesDiscreteType(AstKind kind) {
        return (kind == AST_EnumerationType || kind == AST_IntegerType);
    }
};

//===----------------------------------------------------------------------===//
// EnumerationType
class EnumerationType : public DiscreteType {

public:
    virtual ~EnumerationType() { }

    /// Returns the lower limit for this type.
    ///
    /// \see DiscreteType::getLowerLimit().
    void getLowerLimit(llvm::APInt &res) const;

    /// Returns the upper limit for this type.
    ///
    /// \see DiscreteType::getUpperLimit().
    void getUpperLimit(llvm::APInt &res) const;

    /// Returns the number of bits needed to represent this type.
    ///
    /// \see DiscreteType::getSize().
    uint64_t getSize() const;

    /// Returns the number of literals in this enumeration type.
    uint64_t getNumLiterals() const;

    /// Returns true if this enumeration type is a character type.
    bool isCharacterType() const;

    /// Returns true if this type is constrained.
    bool isConstrained() const { return getConstraint() != 0; }

    //@{
    /// Returns the constraint associated with this enumeration or null if this
    /// is an unconstrained type.
    Range *getConstraint();
    const Range *getConstraint() const;
    //@}

    //@{
    /// Specialize PrimaryType::getRootType().
    EnumerationType *getRootType() {
        return llvm::cast<EnumerationType>(PrimaryType::getRootType());
    }
    const EnumerationType *getRootType() const {
        return llvm::cast<EnumerationType>(PrimaryType::getRootType());
    }
    //@}

    //@{
    /// Specialize PrimaryType::getAncestorType().
    EnumerationType *getAncestorType() {
        return llvm::cast<EnumerationType>(PrimaryType::getAncestorType());
    }
    const EnumerationType *getAncestorType() const {
        return llvm::cast<EnumerationType>(PrimaryType::getAncestorType());
    }
    //@}

    //@{
    /// Returns the base (unconstrained) subtype of this enumeration type.
    EnumerationType *getBaseSubtype();
    const EnumerationType *getBaseSubtype() const;
    //@}

    //@{
    /// Returns the underlying enumeration declaration for this type.
    const TypeDecl *getDefiningDecl() const {
        return const_cast<EnumerationType*>(this)->getDefiningDecl();
    }
    TypeDecl *getDefiningDecl();
    //@}

    /// Returns the declaration corresponding to the Pos attribute for this
    /// type.
    PosAD *getPosAttribute();

    /// Returns the declaration corresponding to the Val attribute for this
    /// type.
    ValAD *getValAttribute();

    // Support isa and dyn_cast.
    static bool classof(const EnumerationType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_EnumerationType;
    }

private:
    /// \name Static constructors.
    ///
    /// The following factory functions are called by AstResource to build
    /// various kinds if EnumerationType nodes.

    /// Builds a root enumeration type.
    static EnumerationType *create(AstResource &resource,
                                   EnumerationDecl *decl);

    /// Builds an unconstrained enumeration subtype.
    static EnumerationType *createSubtype(EnumerationType *rootType,
                                          EnumerationDecl *decl = 0);

    /// Builds a constrained enumeration subtype over the given bounds.
    static EnumerationType *createConstrainedSubtype(EnumerationType *rootType,
                                                     Expr *lower, Expr *upper,
                                                     EnumerationDecl *decl);
    //@}
    friend class AstResource;

protected:
    /// EnumerationType nodes are implemented using three internal classes
    // representing the root, constrained, and unconstrained cases.  The
    // following enumeration identifiers each of these classes and is encoded
    // into the AST::bits field.
    enum EnumKind {
        RootEnumType_KIND,
        UnconstrainedEnumType_KIND,
        ConstrainedEnumType_KIND
    };

    /// Returns true if the given kind denotes a subtype.
    static bool isSubtypeKind(EnumKind kind) {
        return (kind == UnconstrainedEnumType_KIND ||
                kind == ConstrainedEnumType_KIND);
    }

    /// Constructor for the internal subclasses (not for use by AstResource).
    EnumerationType(EnumKind kind, EnumerationType *rootOrParent)
        : DiscreteType(AST_EnumerationType, rootOrParent, isSubtypeKind(kind)) {
        bits = kind;
    }

public:
    /// Returns the EnumKind of this node.  For internal use only.
    EnumKind getEnumKind() const { return EnumKind(bits); }
};

//===----------------------------------------------------------------------===//
// IntegerType
//
// These nodes represent ranged, signed, integer types.  They are allocated and
// owned by an AstResource instance.
class IntegerType : public DiscreteType {

public:
    virtual ~IntegerType() { }

    /// Returns the lower limit for this type.
    ///
    /// \see DiscreteType::getLowerLimit().
    void getLowerLimit(llvm::APInt &res) const;

    /// Returns the upper limit for this type.
    ///
    /// \see DiscreteType::getUpperLimit().
    void getUpperLimit(llvm::APInt &res) const;

    /// Returns true if the base integer type can represent the given value.
    ///
    /// The given APInt is interpreted as signed or unsigned depending on if
    /// this denotes, repectively, a signed or unsigned (modular) integer type.
    bool baseContains(const llvm::APInt &value) const;

    /// Returns the number of bits needed to represent this type.
    ///
    /// \see DiscreteType::getSize();
    uint64_t getSize() const;

    //@{
    /// \brief Returns the base subtype.
    ///
    /// The base subtype is a distinguished unconstrained subtype corresponding
    /// to the attribute S'Base.
    const IntegerType *getBaseSubtype() const {
        return const_cast<IntegerType*>(this)->getBaseSubtype();
    }
    IntegerType *getBaseSubtype();
    //@}

    /// Returns true if this type is constrained.
    bool isConstrained() const { return getConstraint() != 0; }

    /// Returns true if this type denotes a modular (unsigned) integer type.
    bool isModular() const;

    //@{
    /// \brief Returns the Range associated with this IntegerType, or null if
    /// this is an unconstrained type.
    Range *getConstraint();
    const Range *getConstraint() const;
    //@}

    //@{
    /// Specialize PrimaryType::getRootType().
    IntegerType *getRootType() {
        return llvm::cast<IntegerType>(PrimaryType::getRootType());
    }
    const IntegerType *getRootType() const {
        return llvm::cast<IntegerType>(PrimaryType::getRootType());
    }
    //@}

    //@{
    /// Specialize PrimaryType::getAncestorType().
    IntegerType *getAncestorType() {
        return llvm::cast<IntegerType>(PrimaryType::getAncestorType());
    }
    const IntegerType *getAncestorType() const {
        return llvm::cast<IntegerType>(PrimaryType::getAncestorType());
    }
    //@}

    //@{
    /// Returns the declaration defining this discrete type.
    const TypeDecl *getDefiningDecl() const {
        return const_cast<IntegerType*>(this)->getDefiningDecl();
    }
    TypeDecl *getDefiningDecl() = 0;
    //@}

    /// Returns the declaration corresponding to the Pos attribute for this
    /// type.
    PosAD *getPosAttribute();

    /// Returns the declaration corresponding to the Val attribute for this
    /// type.
    ValAD *getValAttribute();

    /// Support isa and dyn_cast.
    static bool classof(const IntegerType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_IntegerType;
    }

private:
    /// \name Static constructors.
    ///
    /// The following factory functions are called by AstResource to build
    /// various types of IntegerType nodes.
    //@{

    /// Builds a root integer type with the given bounds.
    static IntegerType *create(AstResource &resource, IntegerDecl *decl,
                               const llvm::APInt &lower,
                               const llvm::APInt &upper);

    /// Builds an unconstrained integer subtype.
    static IntegerType *createSubtype(IntegerType *rootType,
                                      IntegerDecl *decl = 0);

    /// Builds a constrained integer subtype over the given bounds.
    ///
    /// If \p decl is null an anonymous integer subtype is created.  Otherwise
    /// the constructed type is associated with the given declaration.
    static IntegerType *createConstrainedSubtype(IntegerType *rootType,
                                                 Expr *lower, Expr *upper,
                                                 IntegerDecl *decl);

    //@}
    friend class AstResource;

protected:
    /// IntegerType nodes are implemented using three internal classes
    /// represeting the root, constrained, and unconstrained cases.  The
    /// following enumeration identifies each of these classes and is encoded
    /// into the AST::bits field.
    enum IntegerKind {
        RootIntegerType_KIND,
        UnconstrainedIntegerType_KIND,
        ConstrainedIntegerType_KIND
    };

    /// Returns true if the given kind denotes a subtype.
    static bool isSubtypeKind(IntegerKind kind) {
        return (kind == UnconstrainedIntegerType_KIND ||
                kind == ConstrainedIntegerType_KIND);
    }

    /// Constructor for the internal subclasses (not for use by AstResource).
    IntegerType(IntegerKind kind, IntegerType *rootOrParent)
        : DiscreteType(AST_IntegerType, rootOrParent, isSubtypeKind(kind)) {
        bits = kind;
    }

public:
    /// Returns the IntegerKind of this node.  For internal use only.
    IntegerKind getIntegerKind() const { return IntegerKind(bits); }
};

//===----------------------------------------------------------------------===//
/// \class PrivateType
///
/// \brief Represents a private type.
class PrivateType : public PrimaryType
{
public:
    //@{
    /// Returns the declaration defining this private type.
    const PrivateTypeDecl *getDefiningDecl() const {
        return const_cast<PrivateType*>(this)->getDefiningDecl();
    }
    PrivateTypeDecl *getDefiningDecl();
    //@}

    //@{
    /// Specialize PrimaryType::getRootType().
    PrivateType *getRootType() {
        return llvm::cast<PrivateType>(PrimaryType::getRootType());
    }
    const PrivateType *getRootType() const {
        return llvm::cast<PrivateType>(PrimaryType::getRootType());
    }
    //@}

    /// Returns true if this type as a completion.
    bool hasCompletion() const;

    //@{
    /// Returns the underlying complete type.
    const PrimaryType *getCompleteType() const {
        return const_cast<PrivateType*>(this)->getCompleteType();
    }
    PrimaryType *getCompleteType();
    //@}

    // Support isa/dyn_cast.
    static bool classof(const PrimaryType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_PrivateType;
    }

private:
    friend class AstResource;

    static PrivateType *createPrivateType(PrivateTypeDecl *decl);
    static PrivateType *createPrivateSubtype(PrivateType *base);

    // Internal constructor (not for use by AstResource).
    PrivateType(PrivateTypeDecl *decl);
    PrivateType(PrivateType *base);

    PrivateTypeDecl *definingDecl;
};

//===----------------------------------------------------------------------===//
// CompositeType
//
/// \class
///
/// \brief Common base for all composite types.
class CompositeType : public PrimaryType {

public:
    static bool classof(const CompositeType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->denotesCompositeType();
    }

protected:
    CompositeType(AstKind kind, CompositeType *rootOrParent, bool subtype)
        : PrimaryType(kind, rootOrParent, subtype) {
        assert(this->denotesCompositeType());
    }
};

//===----------------------------------------------------------------------===//
// ArrayType
//
// These nodes describe the index profile and component type of an array type.
// They are allocated and owned by an AstResource instance.
class ArrayType : public CompositeType {

    /// Type used to hold the index types of this array.
    typedef llvm::SmallVector<DiscreteType*, 4> IndexVec;

public:
    /// Returns the identifier associated with this array type.
    IdentifierInfo *getIdInfo() const;

    /// Returns the rank (dimensionality) of this array type.
    unsigned getRank() const { return indices.size(); }

    /// Returns true if this is a vector type (an array of rank 1).
    bool isVector() const { return getRank() == 1; }

    /// Return the length of the first dimension.  This operation is valid only
    /// if this is a statically constrained array type.
    uint64_t length() const;

    //@{
    /// Returns the i'th index type of this array.
    const DiscreteType *getIndexType(unsigned i) const { return indices[i]; }
    DiscreteType *getIndexType(unsigned i) { return indices[i]; }
    //@}

    /// \name Index Type Iterators.
    ///
    /// Iterators over the index types of this array.
    //@{
    typedef IndexVec::iterator iterator;
    iterator begin() { return indices.begin(); }
    iterator end() { return indices.end(); }

    typedef IndexVec::const_iterator const_iterator;
    const_iterator begin() const { return indices.begin(); }
    const_iterator end() const { return indices.end(); }
    //@}

    /// Returns the component type of this array.
    Type *getComponentType() const { return componentType; }

    /// Returns true if this type is constrained.
    bool isConstrained() const { return constraintBit(); }

    /// Returns true if this array type is statically constrained.
    bool isStaticallyConstrained() const;

    //@{
    /// Specialize PrimaryType::getRootType().
    ArrayType *getRootType() {
        return llvm::cast<ArrayType>(PrimaryType::getRootType());
    }
    const ArrayType *getRootType() const {
        return llvm::cast<ArrayType>(PrimaryType::getRootType());
    }
    //@}

    //@{
    /// Specialize PrimaryType::getAncestorType().
    ArrayType *getAncestorType() {
        return llvm::cast<ArrayType>(PrimaryType::getAncestorType());
    }
    const ArrayType *getAncestorType() const {
        return llvm::cast<ArrayType>(PrimaryType::getAncestorType());
    }
    //@}

    //@{
    /// Returns the declaration defining this array type.
    const ArrayDecl *getDefiningDecl() const {
        return getRootType()->definingDecl.get<ArrayDecl*>();
    }
    ArrayDecl *getDefiningDecl() {
        return getRootType()->definingDecl.get<ArrayDecl*>();
    }
    //@}

    // Support isa and dyn_cast.
    static bool classof(const ArrayType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_ArrayType;
    }

private:
    /// Creates a root array type.
    ArrayType(ArrayDecl *decl, unsigned rank, DiscreteType **indices,
              Type *component, bool isConstrained);

    /// Creates a constrained array subtype.
    ArrayType(IdentifierInfo *name, ArrayType *rootType,
              DiscreteType **indices);

    /// Creates an anonymous constrained array subtype.
    ArrayType(ArrayType *rootType, DiscreteType **indices);

    /// Creates an unconstrained array subtype.
    ArrayType(IdentifierInfo *name, ArrayType *rootType);

    friend class AstResource;

    /// The following enumeration defines propertys of an array type which are
    /// encoded into the bits field of the node.
    enum PropertyTags {
        /// Set if the type is constrained.
        Constrained_PROP = 1,
    };

    /// Returns true if this is a constrained array.
    bool constraintBit() const { return bits & Constrained_PROP; }

    /// Marks this as a constrained array type.
    void setConstraintBit() { bits |= Constrained_PROP; }

    /// Vector of index types.
    IndexVec indices;

    /// The component type of this array.
    Type *componentType;

    /// The declaration node or, in the case of an array subtype, the defining
    /// identifier.
    ///
    /// \note This union will contain a subtype declaration instead of an
    /// identifier info once such nodes are supported.
    llvm::PointerUnion<ArrayDecl*, IdentifierInfo*> definingDecl;
};

//===----------------------------------------------------------------------===//
// RecordType
class RecordType : public CompositeType {

public:
    /// Returns the identifier associated with this record type.
    IdentifierInfo *getIdInfo() const;

    //@{
    /// Specialize PrimaryType::getRootType().
    RecordType *getRootType() {
        return llvm::cast<RecordType>(PrimaryType::getRootType());
    }
    const RecordType *getRootType() const {
        return llvm::cast<RecordType>(PrimaryType::getRootType());
    }
    //@}

    //@{
    /// Specialize PrimaryType::getAncestorType().
    RecordType *getAncestorType() {
        return llvm::cast<RecordType>(PrimaryType::getAncestorType());
    }
    const RecordType *getAncestorType() const {
        return llvm::cast<RecordType>(PrimaryType::getAncestorType());
    }
    //@}

    //@{
    /// Returns the declaration node that defined this record type.
    const RecordDecl *getDefiningDecl() const {
        return const_cast<RecordType*>(this)->getDefiningDecl();
    }
    RecordDecl *getDefiningDecl();
    //@}

    /// Returns the number of components defined by this record type.
    unsigned numComponents() const;

    //@{
    /// Returns the type of the i'th component of this record.
    const Type *getComponentType(unsigned i) const {
        return const_cast<RecordType*>(this)->getComponentType(i);
    }
    Type *getComponentType(unsigned i);
    //@}

    /// Currently, record types are always constrained.
    bool isConstrained() const { return true; }

    // Support isa/dyn_cast.
    static bool classof(const RecordType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_RecordType;
    }

private:
    RecordType(RecordDecl *decl);
    RecordType(RecordType *rootType, IdentifierInfo *name);

    friend class AstResource;

    /// The declaration node or, in the case of a record subtype, the defining
    /// identifier.
    ///
    /// \note This union will contain a subtype declaration instead of an
    /// identifier info once such nodes are supported.
    llvm::PointerUnion<RecordDecl*, IdentifierInfo*> definingDecl;
};

//===----------------------------------------------------------------------===//
// AccessType
class AccessType : public PrimaryType {

public:
    /// Returns the defining identifier of this type;
    IdentifierInfo *getIdInfo() const;

    /// Returns the defining identifier of this type as a C-string.
    const char *getString() const { return getIdInfo()->getString(); }

    //@{
    /// Returns the access type declaration that introduced this type.
    const AccessDecl *getDefiningDecl() const {
        return const_cast<AccessType*>(this)->getDefiningDecl();
    }
    AccessDecl *getDefiningDecl();
    //@}

    //@(
    /// Returns the to which this access type points.
    const Type *getTargetType() const { return targetType; }
    Type *getTargetType() { return targetType; }
    //@}

    //@{
    /// Specialize PrimaryType::getRootType().
    AccessType *getRootType() {
        return llvm::cast<AccessType>(PrimaryType::getRootType());
    }
    const AccessType *getRootType() const {
        return llvm::cast<AccessType>(PrimaryType::getRootType());
    }
    //@}

    //@{
    /// Specialize PrimaryType::getAncestorType().
    AccessType *getAncestorType() {
        return llvm::cast<AccessType>(PrimaryType::getAncestorType());
    }
    const AccessType *getAncestorType() const {
        return llvm::cast<AccessType>(PrimaryType::getAncestorType());
    }
    //@}

    // Support isa/dyn_cast;
    static bool classof(const AccessType *node) { return true; }
    static bool classof(const Ast *node) {
        return node->getKind() == AST_AccessType;
    }

private:
    /// Constructs a root access type pointing to \p targetType corresponding to
    /// the given access declaration.
    AccessType(AccessDecl *decl, Type *targetType);

    /// Constructs a subtype of the given access type.
    AccessType(AccessType *rootType, IdentifierInfo *name);

    /// Access types are constructed and managed by AstResource.
    friend class AstResource;

    Type *targetType;

    /// The declaration node or, in the case of an access subtype, the defining
    /// identifier.
    llvm::PointerUnion<AccessDecl*, IdentifierInfo*> definingDecl;
};

} // End comma namespace

#endif
