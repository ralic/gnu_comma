//===-- codegen/CodeGenCapsule.cpp ---------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "CodeGenGeneric.h"
#include "CodeGenCapsule.h"
#include "comma/ast/Decl.h"
#include "comma/codegen/CodeGen.h"
#include "comma/codegen/CodeGenRoutine.h"
#include "comma/codegen/Mangle.h"

using namespace comma;

using llvm::dyn_cast;
using llvm::dyn_cast_or_null;
using llvm::cast;
using llvm::isa;

CodeGenCapsule::CodeGenCapsule(CodeGen &CG, InstanceInfo *instance)
    : CG(CG),
      CGT(CG, instance->getInstanceDecl()),
      capsule(instance->getDefinition()),
      capsuleLinkName(instance->getLinkName()),
      theInstanceInfo(instance) { }

CodeGenCapsule::CodeGenCapsule(CodeGen &CG, FunctorDecl *functor)
    : CG(CG),
      CGT(CG),
      capsule(functor),
      capsuleLinkName(mangle::getLinkName(functor)),
      theInstanceInfo(0) { }

void CodeGenCapsule::emit()
{
    // If this is a just a functor, analyze the dependents so that we can
    // emit a constructor function.
    if (!theInstanceInfo && isa<FunctorDecl>(capsule)) {
        CodeGenGeneric CGG(*this);
        CGG.analyzeDependants();
        return;
    }

    // If we are generating a parameterized instance populate the parameter map
    // with the actual parameters.
    if (generatingParameterizedInstance()) {
        FunctorDecl *functor = cast<FunctorDecl>(capsule);
        DomainInstanceDecl *instance = getInstance();
        for (unsigned i = 0; i < instance->getArity(); ++i)
            paramMap[functor->getFormalType(i)] =
                instance->getActualParamType(i);
    }

    // Codegen each subroutine.
    if (AddDecl *add = capsule->getImplementation()) {
        typedef DeclRegion::DeclIter iterator;
        for (iterator I = add->beginDecls(); I != add->endDecls(); ++I) {
            if (SubroutineDecl *SR = dyn_cast<SubroutineDecl>(*I)) {
                CodeGenRoutine CGR(*this);
                CGR.emitSubroutine(SR);
            }
        }
    }

    theInstanceInfo->markAsCompiled();
}

bool CodeGenCapsule::generatingParameterizedInstance() const
{
    if (generatingInstance())
        return getInstance()->isParameterized();
    return false;
}

DomainInstanceDecl *CodeGenCapsule::getInstance()
{
    assert(generatingInstance() && "Not generating an instance!");
    return theInstanceInfo->getInstanceDecl();
}

const DomainInstanceDecl *CodeGenCapsule::getInstance() const
{
    assert(generatingInstance() && "Not generating an instance!");
    return theInstanceInfo->getInstanceDecl();
}

unsigned CodeGenCapsule::addCapsuleDependency(DomainInstanceDecl *instance)
{
    // FIXME: Assert that the given instance does not represent a percent
    // declaration.

    // If the given instance is parameterized, insert each argument as a
    // dependency, ignoring abstract domains and % (the formal parameters of a
    // functor, nor the domain itself, need recording).
    if (instance->isParameterized()) {
        typedef DomainInstanceDecl::arg_iterator iterator;
        for (iterator iter = instance->beginArguments();
             iter != instance->endArguments(); ++iter) {
            DomainType *argTy = (*iter)->getType();
            if (!(argTy->isAbstract() or argTy->denotesPercent())) {
                DomainInstanceDecl *argInstance = argTy->getInstanceDecl();
                assert(argInstance && "Bad domain type!");
                requiredInstances.insert(argInstance);
            }
        }
    }
    return requiredInstances.insert(instance);
}

DomainInstanceDecl *
CodeGenCapsule::rewriteAbstractDecl(AbstractDomainDecl *abstract) const {
    typedef ParameterMap::const_iterator iterator;
    iterator I = paramMap.find(abstract->getType());
    if (I == paramMap.end())
        return 0;
    DomainType *domTy = cast<DomainType>(I->second);
    return cast<DomainInstanceDecl>(domTy->getDomainTypeDecl());
}
