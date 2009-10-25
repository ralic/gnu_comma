//===-- ast/SignatureSet.h ------------------------------------ -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//
//
// The following class provides a container for the set of signatures associated
// with a particular model.  Note that is class is not a member of the Ast
// hierarchy.
//
// SignatureSet's are populated by specifying the direct signatures (those
// signatures which are explicitly named in the declaration of the associated
// model).  Recursively, the SignatureSet's associated with the direct
// signatures are then consulted to obtain the set of indirect signatures.
//
//===----------------------------------------------------------------------===//

#ifndef COMMA_AST_SIGNATURESET_HDR_GUARD
#define COMMA_AST_SIGNATURESET_HDR_GUARD

#include "comma/ast/AstBase.h"

#include "llvm/ADT/SetVector.h"

namespace comma {

class SignatureSet {

    typedef llvm::SetVector<SigInstanceDecl*> SignatureTable;
    SignatureTable directSignatures;
    SignatureTable allSignatures;

public:
    /// Adds a direct signature to this set using the supplied rewrite rules.
    ///
    /// This method adds \p signature to the direct signature set.  Then, using
    /// the supplied rewrite rules, inserts all super-signatures of \p signature
    /// into the set of indirect signatures, rewriting each in turn.  Returns
    /// true if the signature did not previously exist in this set and false
    /// otherwise.
    bool addDirectSignature(SigInstanceDecl *signature,
                            const AstRewriter &rewriter);

    /// \brief Returns true is the given signature is already contained in this
    /// set.
    bool contains(SigInstanceDecl *signature) const {
        return allSignatures.count(signature);
    }

    /// \brief Returns true if the given signature is a direct signature of this
    /// set.
    bool isDirect(SigInstanceDecl *signature) const {
        return directSignatures.count(signature);
    }

    /// \brief Returns true if the given signature is an indirect signature of
    /// this set.
    bool isIndirect(SigInstanceDecl *signature) const {
        return contains(signature) && !isDirect(signature);
    }

    /// Returns the number of direct and indirect signatures in this set.
    unsigned numSignatures() const { return allSignatures.size(); }

    /// Analogus to numSignatures().
    unsigned size() const { return numSignatures(); }

    /// Returns true if this signature set is empty.
    bool empty() const { return numSignatures() == 0; }

    /// Returns the number of direct signatures in this set.
    unsigned numDirectSignatures() const { return directSignatures.size(); }

    /// Returns the number of indirect signatures in this set.
    unsigned numIndirectSignatures() const {
        return numSignatures() - numDirectSignatures();
    }

    typedef SignatureTable::iterator iterator;
    typedef SignatureTable::const_iterator const_iterator;

    /// \brief Iterators over the direct signatures in this set.
    ///
    /// Returns the direct supersignatures in insertion order (that is, in the
    /// order defined by the sequence of calls to addDirectSignature).
    iterator beginDirect() { return directSignatures.begin(); }
    iterator endDirect()   { return directSignatures.end(); }

    const_iterator beginDirect() const { return directSignatures.begin(); }
    const_iterator endDirect()   const { return directSignatures.end(); }

    /// \brief Iterators over all of the signatures in this set.
    ///
    /// Returns the signatures in a depth-first preorder traversal of the
    /// signature graph, ignoring all repeats.
    iterator begin() { return allSignatures.begin(); }
    iterator end()   { return allSignatures.end(); }

    const_iterator begin() const { return allSignatures.begin(); }
    const_iterator end()   const { return allSignatures.end(); }
};

} // End comma namespace

#endif
