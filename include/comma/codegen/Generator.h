//===-- codegen/Generator.h ----------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2010, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#ifndef COMMA_CODEGEN_GENERATOR_HDR_GUARD
#define COMMA_CODEGEN_GENERATOR_HDR_GUARD

#include "llvm/Module.h"
#include "llvm/Target/TargetData.h"

namespace comma {

class CompilationUnit;
class TextManager;

class Generator {

public:
    virtual ~Generator() { }

    /// Constructs a code generator.
    static Generator *create(llvm::Module *M, const llvm::TargetData &data,
                             TextManager &manager, AstResource &resource);

    /// \brief Codegens a compilation unit.
    virtual void emitCompilationUnit(CompilationUnit *cunit) = 0;

    /// \brief Codegens an entry function which calls into the Procedure \p proc
    /// and embeds it into the LibraryItem \p item.
    ///
    /// The given procedure must meet the following constraints (failure to do
    /// so will fire an assertion):
    ///
    ///   - The procedure must be nullary.  Parameters are not accepted.
    ///
    ///   - The procedure must be defined within a non-generic package.
    ///
    ///   - The procedure must have been codegened.
    ///
    virtual void emitEntry(ProcedureDecl *decl) = 0;

protected:
    // Construct via subclasses.
    Generator() { }

private:
    Generator(const Generator &CG);             // Do not implement.
    Generator &operator =(const Generator &CG); // Likewise.
};

} // end comma namespace.

#endif
