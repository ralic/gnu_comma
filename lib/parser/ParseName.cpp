//===-- parser/ParseName.cpp ---------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009-2010, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "comma/parser/Parser.h"

using namespace comma;

//===----------------------------------------------------------------------===//
// Name Parsing.

Node Parser::parseDirectName(NameOption option)
{
    Location loc = currentLocation();

    switch (currentTokenCode()) {
    default:
        report(diag::UNEXPECTED_TOKEN) << currentTokenString();
        seekNameEnd();
        break;

    case Lexer::TKN_IDENTIFIER:
        if (IdentifierInfo *name = parseIdentifier())
            return client.acceptDirectName(name, loc, option);
        break;

    case Lexer::TKN_CHARACTER:
        if (IdentifierInfo *name = parseCharacter())
            return client.acceptCharacterLiteral(name, loc);
        break;

    case Lexer::TKN_PERCENT:
        return client.acceptPercent(ignoreToken());

    case Lexer::TKN_INJ:
        return parseInj();

    case Lexer::TKN_PRJ:
        return parsePrj();
    };

    return getInvalidNode();
}

Node Parser::parseInj()
{
    assert(currentTokenIs(Lexer::TKN_INJ));
    Location loc = ignoreToken();

    if (!requireToken(Lexer::TKN_LPAREN))
        return getInvalidNode();

    Node expr = parseExpr();

    if (expr.isInvalid() || !requireToken(Lexer::TKN_RPAREN))
        return getInvalidNode();

    return client.acceptInj(loc, expr);
}

Node Parser::parsePrj()
{
    assert(currentTokenIs(Lexer::TKN_PRJ));
    Location loc = ignoreToken();

    if (!requireToken(Lexer::TKN_LPAREN))
        return getInvalidNode();

    Node expr = parseExpr();

    if (expr.isInvalid() || !requireToken(Lexer::TKN_RPAREN)) {
        seekCloseParen();
        return getInvalidNode();
    }

    return client.acceptPrj(loc, expr);
}

Node Parser::parseSelectedComponent(Node prefix, NameOption option)
{
    Location loc = currentLocation();

    if (reduceToken(Lexer::TKN_ALL))
        return client.acceptDereference(prefix, loc);

    IdentifierInfo *name = parseAnyIdentifier();

    if (name) {
        bool forStatement = (option == Statement_Name);
        return client.acceptSelectedComponent(prefix, name, loc, forStatement);
    }

    seekNameEnd();
    return getInvalidNode();
}

Node Parser::parseApplication(Node prefix)
{
    assert(currentTokenIs(Lexer::TKN_LPAREN));

    if (unitExprFollows()) {
        Location loc = ignoreToken(); // Ignore the opening paren;
        ignoreToken();                // Ingore the close paren;
        Node result = client.finishName(prefix);
        if (result.isValid())
            report(loc, diag::EMPTY_PARAMS);
        return result;
    }

    ignoreToken();              // Ignore the opening paren.
    NodeVector arguments;
    bool seenSelector = false;

    do {
        Node arg = getInvalidNode();
        if (keywordSelectionFollows()) {
            arg = parseParameterAssociation();
            seenSelector = true;
        }
        else if (seenSelector) {
            report(diag::POSITIONAL_FOLLOWING_SELECTED_PARAMETER);
            seekCloseParen();
            return getInvalidNode();
        }
        else
            arg = parseExpr();

        if (arg.isValid())
            arguments.push_back(arg);
        else {
            seekCloseParen();
            return getInvalidNode();
        }
    } while (reduceToken(Lexer::TKN_COMMA));

    if (!requireToken(Lexer::TKN_RPAREN)) {
        seekCloseParen();
        return getInvalidNode();
    }
    return client.acceptApplication(prefix, arguments);
}

Node Parser::parseParameterAssociation()
{
    assert(keywordSelectionFollows());

    Location loc = currentLocation();
    IdentifierInfo *key = parseIdentifier();

    ignoreToken();              // Ignore the =>.

    Node rhs = parseExpr();

    if (rhs.isValid())
        return client.acceptParameterAssociation(key, loc, rhs);
    else
        return getInvalidNode();
}

Node Parser::parseAttribute(Node prefix, NameOption option)
{
    assert(attributeFollows());
    ignoreToken();              // Ignore the quote.

    Location loc = currentLocation();
    IdentifierInfo *name = parseIdentifier();

    if (name->getAttributeID() == attrib::UNKNOWN_ATTRIBUTE) {
        report(loc, diag::UNKNOWN_ATTRIBUTE) << name;
        return getInvalidNode();
    }

    /// Names can be parsed in a variety of contexts.  However, Range attributes
    /// are a special case as there are special parse rules which consume them.
    if ((name->getAttributeID() == attrib::Range) &&
        (option != Accept_Range_Attribute)) {
        report(loc, diag::INVALID_ATTRIBUTE_CONTEXT) << name;
        return getInvalidNode();
    }

    return client.acceptAttribute(prefix, name, loc);
}

Node Parser::parseName(NameOption option)
{
    // All names start with a direct name.
    Node prefix = parseDirectName(option);

    if (prefix.isInvalid())
        return prefix;

    for ( ;; ) {
        if (currentTokenIs(Lexer::TKN_LPAREN))
            prefix = parseApplication(prefix);
        else if (reduceToken(Lexer::TKN_DOT)) {
            prefix = client.finishName(prefix);
            if (prefix.isValid())
                prefix = parseSelectedComponent(prefix, option);
        }
        else if (attributeFollows()) {
            prefix = client.finishName(prefix);
            if (prefix.isValid())
                prefix = parseAttribute(prefix, option);
        }
        else
            break;

        if (prefix.isInvalid())
            break;
    }

    if (prefix.isInvalid()) {
        seekNameEnd();
        return prefix;
    }
    else
        return client.finishName(prefix);
}

void Parser::seekNameEnd()
{
    for ( ;; ) {
        switch(currentTokenCode()) {

        default:
            return;

        case Lexer::TKN_IDENTIFIER:
        case Lexer::TKN_DOT:
        case Lexer::TKN_CHARACTER:
        case Lexer::TKN_PERCENT:
        case Lexer::TKN_INJ:
        case Lexer::TKN_PRJ:
        case Lexer::TKN_ALL:
            ignoreToken();
            break;

        case Lexer::TKN_LPAREN:
            ignoreToken();
            seekCloseParen();
        };
    }
}

bool Parser::consumeName()
{
    // Identify direct names.  If we cannot consume a direct name we simply
    // return false.
    switch (currentTokenCode()) {
    default:
        return false;
    case Lexer::TKN_CHARACTER:
    case Lexer::TKN_IDENTIFIER:
    case Lexer::TKN_PERCENT:
    case Lexer::TKN_INJ:
    case Lexer::TKN_PRJ:
        break;
    }

    // OK. Consume the direct name.
    ignoreToken();

    // From this point on we will invariably return true since a name was
    // consumed.  Just drive the token stream as far as we can assuming all
    // tokens are valid.
    bool consume = true;
    while (consume) {
        if (reduceToken(Lexer::TKN_LPAREN))
            consume = seekCloseParen();
        else if (reduceToken(Lexer::TKN_DOT)) {
            switch (currentTokenCode()) {
            default:
                consume = false;
                break;
            case Lexer::TKN_IDENTIFIER:
            case Lexer::TKN_CHARACTER:
            case Lexer::TKN_ALL:
                ignoreToken();
                break;
            };
        }
        else if (attributeFollows()) {
            ignoreToken();      // Ignore the quote.
            ignoreToken();      // Ignore the attribute name.
        }
        else
            consume = false;
    }
    return true;
}

