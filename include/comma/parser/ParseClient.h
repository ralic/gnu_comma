//===-- parser/ParseClient.h ---------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#ifndef COMMA_PARSECLIENT_HDR_GUARD
#define COMMA_PARSECLIENT_HDR_GUARD

#include "comma/basic/ParameterModes.h"
#include "comma/basic/IdentifierInfo.h"
#include "comma/parser/Descriptors.h"

namespace comma {

class ParseClient {

public:
    // The parser does not know about the underlying node representation.  It
    // calls this method when cleaning up after a bad parse.  The implementation
    // need not delete the node as a result of this call -- it might choose to
    // cache it, for instance.
    virtual void deleteNode(Node node) = 0;

    // Starts the processing of a model.  The supplied Descriptor contains the
    // name and location of the declaration, as is either of kind
    // desc::Signature or desc::Domain.
    virtual void beginModelDeclaration(Descriptor &desc) = 0;

    virtual void endModelDefinition() = 0;

    // Called immediately after a model declaration has been registered.  This
    // call defines a formal parameter of a model.  The parser collects the
    // results of this call into a Descriptor object and supplies them back to
    // the client in a call to acceptModelDeclaration.
    virtual Node acceptModelParameter(IdentifierInfo *formal,
                                      Node            typeNode,
                                      Location        loc) = 0;

    // This call completes the declaration of a model (name and
    // parameterization).
    virtual void acceptModelDeclaration(Descriptor &desc) = 0;

    virtual void beginWithExpression() = 0;
    virtual void endWithExpression() = 0;

    // Called for each super signature of a with expression.
    virtual Node acceptWithSupersignature(Node typeNode, Location loc) = 0;

    // Called at the begining of an add expression.  The client accepts
    // components of an add expression after this call until endAddExpression is
    // called.
    virtual void beginAddExpression() = 0;

    // Completes an add expression.
    virtual void endAddExpression() = 0;

    virtual void beginSubroutineDeclaration(Descriptor &desc) = 0;

    virtual Node acceptSubroutineParameter(IdentifierInfo *formal,
                                           Location        loc,
                                           Node            typeNode,
                                           ParameterMode   mode) = 0;

    virtual Node acceptSubroutineDeclaration(Descriptor &desc,
                                             bool definitionFollows) = 0;


    /// Begin a subroutine definition, where \p declarationNode is a valid node
    /// returned from ParseClient::acceptSubroutineDeclaration.
    virtual void beginSubroutineDefinition(Node declarationNode) = 0;
    virtual void endSubroutineDefinition() = 0;

    virtual Node acceptDeclaration(IdentifierInfo *name,
                                   Node            type,
                                   Location        loc) = 0;

    virtual void acceptDeclarationInitializer(Node declNode,
                                              Node initializer) = 0;

    virtual Node acceptPercent(Location loc) = 0;

    virtual Node acceptTypeIdentifier(IdentifierInfo *info, Location loc) = 0;

    virtual Node acceptTypeApplication(IdentifierInfo  *connective,
                                       Node            *argumentNodes,
                                       Location        *argumentLocs,
                                       unsigned         numArgs,
                                       IdentifierInfo **keys,
                                       Location        *keyLocs,
                                       unsigned         numKeys,
                                       Location         loc) = 0;

    virtual Node acceptFunctionType(IdentifierInfo **formals,
                                    Location        *formalLocations,
                                    Node            *types,
                                    Location        *typeLocations,
                                    unsigned         arity,
                                    Node             returnType,
                                    Location         returnLocation) = 0;

    // Submits an import from the given type node, and the location of the
    // import reserved word.
    virtual void acceptImportStatement(Node importedType, Location loc) = 0;

    virtual Node acceptKeywordSelector(IdentifierInfo *key,
                                       Location        loc,
                                       Node            exprNode,
                                       bool            forSubroutine) = 0;

    virtual Node acceptDirectName(IdentifierInfo *name, Location loc) = 0;

    virtual Node acceptFunctionCall(IdentifierInfo  *name,
                                    Location         loc,
                                    Node            *args,
                                    unsigned         numArgs) = 0;

    virtual Node acceptProcedureCall(IdentifierInfo  *name,
                                     Location         loc,
                                     Node            *args,
                                     unsigned         numArgs) = 0;

};

} // End comma namespace.

#endif