//===-- ast/SignatureSet.cpp ---------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "comma/ast/AstRewriter.h"
#include "comma/ast/Decl.h"
#include "comma/ast/SignatureSet.h"
#include "comma/ast/Type.h"

using namespace comma;

bool SignatureSet::addDirectSignature(SignatureType *signature,
                                      const AstRewriter &rewriter)
{
    if (directSignatures.insert(signature)) {
        Sigoid *sigDecl = signature->getSigoid();
        const SignatureSet& sigset = sigDecl->getSignatureSet();
        allSignatures.insert(signature);
        for (iterator iter = sigset.begin(); iter != sigset.end(); ++iter) {
            SignatureType *rewrite = rewriter.rewrite(*iter);
            allSignatures.insert(rewrite);
        }
        return true;
    }
    return false;
}

