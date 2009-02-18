//===-- parser/parser.cpp ------------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "comma/parser/Parser.h"
#include <cassert>

using namespace comma;


Node Parser::parseExpr()
{
    return parsePrimaryExpr();
}

Node Parser::parseSubroutineKeywordSelection()
{
    assert(keywordSelectionFollows());
    Location        loc = currentLocation();
    IdentifierInfo *key = parseIdentifierInfo();
    Node            expr;

    ignoreToken();              // consume the "=>".
    expr = parseExpr();

    if (expr.isInvalid())
        return Node::getInvalidNode();
    else
        return client.acceptKeywordSelector(key, loc, expr, true);
}

Node Parser::parsePrimaryExpr()
{
    Location loc = currentLocation();

    if (reduceToken(Lexer::TKN_LPAREN)) {
        Node result = parseExpr();
        if (!reduceToken(Lexer::TKN_RPAREN))
            report(diag::UNEXPECTED_TOKEN_WANTED)
                << currentTokenString() << ")";
        return result;
    }

    // The only primary expressions we currently support are direct names.
    IdentifierInfo *directName = parseIdentifierInfo();

    if (!directName) {
        seekToken(Lexer::TKN_SEMI);
        return Node::getInvalidNode();
    }

    // If we have an empty set paramters, treat as a nullary name.
    if (unitExprFollows()) {
        report(diag::ILLEGAL_EMPTY_PARAMS);
        ignoreToken();
        ignoreToken();
        return client.acceptDirectName(directName, loc);
    }

    if (reduceToken(Lexer::TKN_LPAREN)) {
        NodeVector arguments;
        bool       seenSelector = false;
        do {
            Node arg;
            if (keywordSelectionFollows()) {
                arg = parseSubroutineKeywordSelection();
                seenSelector = true;
            }
            else if (seenSelector) {
                report(diag::POSITIONAL_FOLLOWING_SELECTED_PARAMETER);
                seekCloseParen();
                return Node::getInvalidNode();
            }
            else
                arg = parseExpr();

            if (arg.isInvalid()) {
                seekCloseParen();
                return Node::getInvalidNode();
            }
            arguments.push_back(arg);
        } while (reduceToken(Lexer::TKN_COMMA));

        if (!requireToken(Lexer::TKN_RPAREN)) {
            seekCloseParen();
            return Node::getInvalidNode();
        }

        return client.acceptFunctionCall(directName,
                                         loc,
                                         &arguments[0],
                                         arguments.size());
    }
    return client.acceptDirectName(directName, loc);
}
