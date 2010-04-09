//===-- parser/Parser.cpp ------------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008-2010, Stephen Wilson
//
//===----------------------------------------------------------------------===//
//
// This parser is a typical hand written recursive decent parser.
//
// There is always a "current token", and each parse method begins its
// processing using the current token to guide its logic.  Therefore, the rule
// to follow when calling a parse method (or when writing one) is "the current
// token is the next token to be parsed".
//
// Similarly, parse methods leave the stream so that the current token is again
// the next token to be parsed.  Thus, a parse method which consumes exactly one
// token moves the token stream by exactly one token.
//
// As the parser proceeds, callbacks provided by the ParseClient are invoked.
// The parser does not build an AST explicitly -- rather, it formulates calls
// to the client, which in turn could construct an AST, or perform some other
// action.
//
//===----------------------------------------------------------------------===//

#include "comma/basic/Attributes.h"
#include "comma/basic/Pragmas.h"
#include "comma/parser/Parser.h"

#include "llvm/ADT/APInt.h"

#include <cassert>
#include <cstring>
#include <vector>

using namespace comma;

Parser::Parser(TextProvider &txtProvider, IdentifierPool &idPool,
               ParseClient &client, Diagnostic &diag)
    : ParserBase(txtProvider, idPool, diag),
      client(client)
{
    // Mark each identifier which can name an attribute.
    attrib::markAttributeIdentifiers(idPool);
}

bool Parser::seekCloseParen()
{
    unsigned depth = 1;

    for (;;) {
        seekTokens(Lexer::TKN_LPAREN, Lexer::TKN_RPAREN);

        switch (currentTokenCode()) {
        default:
            break;

        case Lexer::TKN_LPAREN:
            depth++;
            break;

        case Lexer::TKN_RPAREN:
            depth--;
            if (depth == 0) {
                ignoreToken();
                return true;
            }
            break;

        case Lexer::TKN_EOT:
            return false;
        }

        ignoreToken();
    }
}

bool Parser::seekSemi()
{
    while (seekTokens(Lexer::TKN_LPAREN, Lexer::TKN_SEMI)) {

        if (currentTokenIs(Lexer::TKN_SEMI))
            return true;

        // Otherwise, the current token is an LBRACE.  Dive into the parens and
        // seek the closing token.
        ignoreToken();
        seekCloseParen();
    }
    return false;
}

// This function drives the stream of input tokens looking for an end statement.
// If the end statement is followed by a matching tag, true is returned.
// Otherwise the search continues until a matching end is found or the end of
// the token stream is encountered.  In the latter case, false is returned.
bool Parser::seekEndTag(IdentifierInfo *tag)
{
    while (seekToken(Lexer::TKN_END))
    {
        IdentifierInfo *info = 0;

        if (nextTokenIs(Lexer::TKN_IDENTIFIER)) {
            info = getIdentifierInfo(peekToken());
        }

        if (info == tag)
            return true;
        else
            ignoreToken();
    }
    return false;
}

bool Parser::seekAndConsumeEndTag(IdentifierInfo *tag)
{
    if (seekEndTag(tag)) {
        ignoreToken();                // Ignore 'end'.
        ignoreToken();                // Ignore the tag.
        return true;
    }
    return false;
}

bool Parser::seekEndIf()
{
    unsigned depth = 1;

    while (seekTokens(Lexer::TKN_IF, Lexer::TKN_END)) {
        switch (currentTokenCode()) {

        default:
            return false;

        case Lexer::TKN_IF:
            ignoreToken();
            depth++;
            break;

        case Lexer::TKN_END:
            ignoreToken();
            if (reduceToken(Lexer::TKN_IF)) {
                if (--depth == 0)
                    return true;
            }
        }
    }
    return false;
}

bool Parser::seekEndLoop(IdentifierInfo *tag)
{
    unsigned depth = 1;
    while (seekTokens(Lexer::TKN_FOR, Lexer::TKN_WHILE,
                      Lexer::TKN_LOOP, Lexer::TKN_END)) {
        switch (currentTokenCode()) {
        default:
            return false;

        case Lexer::TKN_WHILE:
        case Lexer::TKN_FOR:
            seekToken(Lexer::TKN_LOOP);
            ignoreToken();
            depth++;
            break;

        case Lexer::TKN_LOOP:
            ignoreToken();
            depth++;
            break;

        case Lexer::TKN_END:
            ignoreToken();
            if (!reduceToken(Lexer::TKN_LOOP) || --depth != 0)
                continue;

            if (tag)
                return true;

            IdentifierInfo *info = 0;
            if (nextTokenIs(Lexer::TKN_IDENTIFIER)) {
                info = getIdentifierInfo(peekToken());
            }
            if (info == tag) {
                ignoreToken();
                return true;
            }
            return false;
        }
    }
    return false;
}

bool Parser::unitExprFollows()
{
    return currentTokenIs(Lexer::TKN_LPAREN) && nextTokenIs(Lexer::TKN_RPAREN);
}

bool Parser::assignmentFollows()
{
    beginExcursion();
    seekNameEnd();
    bool status = currentTokenIs(Lexer::TKN_ASSIGN);
    endExcursion();
    return status;
}

bool Parser::keywordSelectionFollows()
{
    return currentTokenIs(Lexer::TKN_IDENTIFIER)
        && nextTokenIs(Lexer::TKN_RDARROW);
}

bool Parser::selectedComponentFollows()
{
    bool status = false;

    if (currentTokenIs(Lexer::TKN_IDENTIFIER)) {
        switch (peekTokenCode()) {

        default:
            break;

        case Lexer::TKN_DOT:
            status = true;
            break;

        case Lexer::TKN_LPAREN: {
            beginExcursion();
            ignoreToken();      // Ignore the identifier.
            do {
                ignoreToken();  // Ignore the left paren.
                seekCloseParen();
            } while (currentTokenIs(Lexer::TKN_LPAREN));
            status = currentTokenIs(Lexer::TKN_DOT);
            endExcursion();
            break;
        }
        }
    }
    return status;
}

bool Parser::aggregateFollows()
{
    assert(currentTokenIs(Lexer::TKN_LPAREN));

    bool result = false;

    beginExcursion();
    ignoreToken();              // Ignore the left paren.

SEEK:
    if (seekTokens(Lexer::TKN_LPAREN,
                   Lexer::TKN_COMMA, Lexer::TKN_OTHERS,
                   Lexer::TKN_RDARROW, Lexer::TKN_RPAREN)) {

        switch (currentTokenCode()) {

        default: break;

        case Lexer::TKN_COMMA:
            result = true;      // Positional aggregate.
            break;

        case Lexer::TKN_RDARROW:
            result = true;      // Keyed aggregate.
            break;

        case Lexer::TKN_LPAREN:
            ignoreToken();
            if (seekCloseParen())
                goto SEEK;
            break;

        case Lexer::TKN_OTHERS:
            result = true;      // Others aggregate.
            break;
        }
    }

    endExcursion();
    return result;
}

bool Parser::taggedStmtFollows()
{
    return (currentTokenIs(Lexer::TKN_IDENTIFIER) &&
            nextTokenIs(Lexer::TKN_COLON));
}

bool Parser::qualificationFollows()
{
    return currentTokenIs(Lexer::TKN_QUOTE) && nextTokenIs(Lexer::TKN_LPAREN);
}

bool Parser::attributeFollows()
{
    return (currentTokenIs(Lexer::TKN_QUOTE) &&
            nextTokenIs(Lexer::TKN_IDENTIFIER));
}

IdentifierInfo *Parser::parseCharacter()
{
    if (currentTokenIs(Lexer::TKN_CHARACTER)) {
        IdentifierInfo *info = getIdentifierInfo(currentToken());
        ignoreToken();
        return info;
    }
    else {
        report(diag::UNEXPECTED_TOKEN) << currentToken().getString();
        return 0;
    }
}

IdentifierInfo *Parser::parseIdentifierOrCharacter()
{
    if (currentTokenIs(Lexer::TKN_IDENTIFIER))
        return parseIdentifier();
    else
        return parseCharacter();
}

IdentifierInfo *Parser::parseAnyIdentifier()
{
    if (currentTokenIs(Lexer::TKN_CHARACTER))
        return parseCharacter();
    else
        return parseFunctionIdentifier();
}

// Parses an end tag.  If expectedTag is non-null, parse "end <tag>", otherwise
// parse "end".  Returns true if tokens were consumed (which can happen when the
// parse fails due to a missing or unexpected end tag) and false otherwise.
bool Parser::parseEndTag(IdentifierInfo *expectedTag)
{
    Location tagLoc;
    IdentifierInfo *tag;

    if (requireToken(Lexer::TKN_END)) {
        if (expectedTag) {
            if (currentTokenIs(Lexer::TKN_SEMI))
                report(diag::EXPECTED_END_TAG) << expectedTag;
            else {
                tagLoc = currentLocation();
                tag = parseFunctionIdentifier();
                if (tag && tag != expectedTag)
                    report(tagLoc, diag::EXPECTED_END_TAG) << expectedTag;
            }
        }
        else if (currentTokenIs(Lexer::TKN_IDENTIFIER)) {
            // FIXME:  The above test is not general enough, since we could have
            // operator tokens (TKN_PLUS, TKN_STAR, etc) labeling an "end".
            tagLoc = currentLocation();
            tag = parseIdentifier();
            report(tagLoc, diag::UNEXPECTED_END_TAG) << tag;
        }
        return true;
    }
    return false;
}

bool Parser::parseLoopEndTag(IdentifierInfo *expectedTag)
{
    Location tagLoc;
    IdentifierInfo *tag;

    if (!(requireToken(Lexer::TKN_END) && requireToken(Lexer::TKN_LOOP)))
        return false;

    if (expectedTag) {
        if (currentTokenIs(Lexer::TKN_SEMI))
            report(diag::EXPECTED_END_TAG) << expectedTag;
        else {
            tagLoc = currentLocation();
            tag = parseFunctionIdentifier();
            if (tag && tag != expectedTag)
                report(tagLoc, diag::EXPECTED_END_TAG) << expectedTag;
        }
    }
    else if (currentTokenIs(Lexer::TKN_IDENTIFIER)) {
        // FIXME:  The above test is not general enough, since we could have
        // operator tokens (TKN_PLUS, TKN_STAR, etc) labeling an "end".
        tagLoc = currentLocation();
        tag = parseIdentifier();
        report(tagLoc, diag::UNEXPECTED_END_TAG) << tag;
    }
    return true;
}

void Parser::parseGenericFormalParams()
{
    assert(currentTokenIs(Lexer::TKN_GENERIC));
    ignoreToken();

    client.beginGenericFormals();
    for ( ;; ) {
        switch (currentTokenCode()) {

        default:
            report(diag::UNEXPECTED_TOKEN) << currentTokenString();
            if (seekTokens(Lexer::TKN_ABSTRACT, Lexer::TKN_DOMAIN,
                           Lexer::TKN_SIGNATURE, Lexer::TKN_PACKAGE)) {
                if (currentTokenIs(Lexer::TKN_ABSTRACT))
                    continue;
            }
            client.endGenericFormals();
            return;

        case Lexer::TKN_ABSTRACT:
            parseGenericFormalDomain();
            break;

        case Lexer::TKN_DOMAIN:
        case Lexer::TKN_SIGNATURE:
        case Lexer::TKN_PACKAGE:
            client.endGenericFormals();
            return;
        }
    }
}

void Parser::parseGenericFormalDomain()
{
    assert(currentTokenIs(Lexer::TKN_ABSTRACT));
    ignoreToken();

    if (!requireToken(Lexer::TKN_DOMAIN)) {
        seekToken(Lexer::TKN_SEMI);
        return;
    }

    Location loc = currentLocation();
    IdentifierInfo *name = parseIdentifier();

    if (!name) {
        seekToken(Lexer::TKN_SEMI);
        return;
    }

    if (reduceToken(Lexer::TKN_IS)) {
        Node sig = parseName();
        if (sig.isValid())
            client.acceptFormalDomain(name, loc, sig);
    }
    else
        client.acceptFormalDomain(name, loc, getNullNode());

    requireToken(Lexer::TKN_SEMI);
}

void Parser::parseSignatureProfile()
{
    client.beginSignatureProfile();

    if (currentTokenIs(Lexer::TKN_IS))
        parseSupersignatureProfile();

    if (reduceToken(Lexer::TKN_WITH))
        parseWithComponents();

    client.endSignatureProfile();
}

// Parses a sequence of super-signatures in a 'with' expression.
void Parser::parseSupersignatureProfile()
{
    assert(currentTokenIs(Lexer::TKN_IS));
    Location isLoc = ignoreToken();

    // Check for a resonably common case of writing "is" where one ment "with".
    switch (currentTokenCode()) {
    default:
        break;
    case Lexer::TKN_PROCEDURE:
    case Lexer::TKN_FUNCTION:
    case Lexer::TKN_TYPE:
    case Lexer::TKN_SUBTYPE:
        // Report that we expected a "with" token and continue parsing as though
        // we had a "with".
        report(isLoc, diag::UNEXPECTED_TOKEN_WANTED)
            << Lexer::tokenString(Lexer::TKN_IS)
            << Lexer::tokenString(Lexer::TKN_WITH);
        parseWithComponents();
        return;
    }

    // Otherwise, parse the super signature list.
    do {
        Node super = parseName();

        if (super.isValid())
            client.acceptSupersignature(super);
        else {
            seekTokens(Lexer::TKN_AND, Lexer::TKN_ADD,
                       Lexer::TKN_WITH, Lexer::TKN_END);
        }
    } while (reduceToken(Lexer::TKN_AND));
}

void Parser::parseWithComponents()
{
    bool status = false;

    for (;;) {
        switch (currentTokenCode()) {
        default:
            return;

        case Lexer::TKN_FUNCTION:
            status = parseFunctionDeclaration(true).isValid();
            break;

        case Lexer::TKN_PROCEDURE:
            status = parseProcedureDeclaration(true).isValid();
            break;

        case Lexer::TKN_TYPE:
            status = parseType();
            break;

        case Lexer::TKN_SUBTYPE:
            status = parseSubtype();
            break;
        }

        if (!status)
            seekTokens(Lexer::TKN_FUNCTION, Lexer::TKN_PROCEDURE,
                       Lexer::TKN_TYPE,     Lexer::TKN_SEMI,
                       Lexer::TKN_END,      Lexer::TKN_ADD);

        requireToken(Lexer::TKN_SEMI);
    }
}

void Parser::parseCarrier()
{
    assert(currentTokenIs(Lexer::TKN_CARRIER));

    Location loc = ignoreToken();
    IdentifierInfo *name = parseIdentifier();

    if (!name) {
        seekToken(Lexer::TKN_SEMI);
        return;
    }

    if (!requireToken(Lexer::TKN_IS)) {
        seekToken(Lexer::TKN_SEMI);
        return;
    }

    Node type = parseName();

    if (type.isInvalid()) {
        seekToken(Lexer::TKN_SEMI);
        return;
    }

    client.acceptCarrier(name, loc, type);
}

void Parser::parseAddComponents()
{
    client.beginAddExpression();

    for (;;) {
        switch (currentTokenCode()) {
        default:
            client.endAddExpression();
            return;

        case Lexer::TKN_FUNCTION:
            parseFunctionDeclOrDefinition();
            break;

        case Lexer::TKN_PROCEDURE:
            parseProcedureDeclOrDefinition();
            break;

        case Lexer::TKN_IMPORT:
            parseImportDeclaration();
            break;

        case Lexer::TKN_CARRIER:
            parseCarrier();
            break;

        case Lexer::TKN_TYPE:
            parseType();
            break;

        case Lexer::TKN_SUBTYPE:
            parseSubtype();
            break;

        case Lexer::TKN_PRAGMA:
            parseDeclarationPragma();
            break;
        }

        requireToken(Lexer::TKN_SEMI);
    }
}

void Parser::parseWithClause()
{
    assert(currentTokenIs(Lexer::TKN_WITH));

    Location loc = ignoreToken();
    llvm::SmallVector<IdentifierInfo *, 8> names;

    do {
        IdentifierInfo *idInfo = parseIdentifier();

        if (!idInfo) {
            seekSemi();
            return;
        }

        names.push_back(idInfo);
    } while (reduceToken(Lexer::TKN_DOT));

    if (!requireToken(Lexer::TKN_SEMI)) {
        seekSemi();
        return;
    }

    client.acceptWithClause(loc, &names[0], names.size());
}


void Parser::parseCapsule()
{
    bool parsingSignature = false;
    IdentifierInfo *name = 0;

    client.beginCapsule();

    if (currentTokenIs(Lexer::TKN_GENERIC))
        parseGenericFormalParams();

    if (reduceToken(Lexer::TKN_PACKAGE)) {
        Location loc = currentLocation();
        if (!(name = parseIdentifier()))
            return;
        client.beginPackageDecl(name, loc);
    }
    else if (reduceToken(Lexer::TKN_DOMAIN)) {
        Location loc = currentLocation();
        if (!(name = parseIdentifier()))
            return;
        client.beginDomainDecl(name, loc);
    }
    else if (reduceToken(Lexer::TKN_SIGNATURE)) {
        Location loc = currentLocation();
        if (!(name = parseIdentifier()))
            return;
        client.beginSignatureDecl(name, loc);
        parsingSignature = true;
    }
    else {
        assert(false && "Bad token for this production!");
        return;
    }

    if (currentTokenIs(Lexer::TKN_IS) || currentTokenIs(Lexer::TKN_WITH))
        parseSignatureProfile();

    if (!parsingSignature && reduceToken(Lexer::TKN_ADD))
        parseAddComponents();

    client.endCapsule();

    // Consume and verify the end tag.  On failure seek the next top level form.
    if (!parseEndTag(name))
        seekTokens(Lexer::TKN_SIGNATURE, Lexer::TKN_DOMAIN, Lexer::TKN_PACKAGE);
    else
        requireToken(Lexer::TKN_SEMI);
}

// Parses an "in", "out" or "in out" parameter mode specification.  If no such
// specification is available on the stream MODE_DEFAULT is returned.  A common
// mistake is to find "out in" instead of "in out".  In this case, we simply
// issue a diagnostic and return MODE_IN_OUT.
PM::ParameterMode Parser::parseParameterMode()
{
    PM::ParameterMode mode = PM::MODE_DEFAULT;

    if (reduceToken(Lexer::TKN_IN)) {
        if (reduceToken(Lexer::TKN_OUT))
            mode = PM::MODE_IN_OUT;
        else
            mode = PM::MODE_IN;
    }
    else if (reduceToken(Lexer::TKN_OUT)) {
        if (currentTokenIs(Lexer::TKN_IN)) {
            report(diag::OUT_IN_PARAMETER_MODE);
            ignoreToken();
            mode = PM::MODE_IN_OUT;
        }
        else
            mode = PM::MODE_OUT;
    }
    return mode;
}

bool Parser::parseSubroutineParameter()
{
    IdentifierInfo *formal;
    Location location;
    PM::ParameterMode mode;

    location = currentLocation();
    formal = parseIdentifier();

    if (!formal) return false;

    if (!requireToken(Lexer::TKN_COLON)) return false;

    mode = parseParameterMode();
    Node type = parseName();
    if (type.isInvalid()) return false;

    client.acceptSubroutineParameter(formal, location, type, mode);
    return true;
}

void Parser::parseSubroutineParameters()
{
    assert(currentTokenIs(Lexer::TKN_LPAREN));

    // Check that we do not have an empty parameter list.
    if (unitExprFollows()) {
        report(diag::EMPTY_PARAMS);

        // Consume the opening and closing parens.
        ignoreToken();
        ignoreToken();
        return;
    }

    // Consume the opening paren.
    ignoreToken();

    for (;;) {
        if (!parseSubroutineParameter())
            seekTokens(Lexer::TKN_SEMI, Lexer::TKN_RPAREN);

        switch (currentTokenCode()) {

        default:
            // An unexpected token.  Abort processing of the parameter list and
            // seek a closing paren.
            report(diag::UNEXPECTED_TOKEN)  << currentTokenString();
            if (seekCloseParen()) ignoreToken();
            return;

        case Lexer::TKN_COMMA:
            // Using a comma instead of a semicolon is a common mistake.  Issue
            // a diagnostic and continue processing as though a semi was found.
            report(diag::UNEXPECTED_TOKEN_WANTED) << "," << ";";
            ignoreToken();
            break;

        case Lexer::TKN_SEMI:
            // OK, process the next parameter.
            ignoreToken();
            break;

        case Lexer::TKN_RPAREN:
            // The parameter list is complete.  Consume the paren and return.
            ignoreToken();
            return;
        }
    }
}

Node Parser::parseFunctionDeclaration(bool parsingSignatureProfile)
{
    assert(currentTokenIs(Lexer::TKN_FUNCTION));
    ignoreToken();

    Location location = currentLocation();
    IdentifierInfo *name = parseFunctionIdentifier();

    if (!name)
        return getInvalidNode();

    client.beginFunctionDeclaration(name, location);

    if (currentTokenIs(Lexer::TKN_LPAREN))
        parseSubroutineParameters();

    Node returnNode = getNullNode();
    if (reduceToken(Lexer::TKN_RETURN)) {
        returnNode = parseName();
        if (returnNode.isInvalid()) {
            seekTokens(Lexer::TKN_SEMI, Lexer::TKN_IS);
            returnNode = getNullNode();
        }
    }
    else
        report(diag::MISSING_RETURN_AFTER_FUNCTION);

    client.acceptFunctionReturnType(returnNode);

    bool bodyFollows = currentTokenIs(Lexer::TKN_IS);

    // FIXME: We should model the parser state with more than a tag stack.
    if (bodyFollows)
        endTagStack.push(EndTagEntry(NAMED_TAG, location, name));

    return client.endSubroutineDeclaration(bodyFollows);
}

Node Parser::parseProcedureDeclaration(bool parsingSignatureProfile)
{
    assert(currentTokenIs(Lexer::TKN_PROCEDURE));
    ignoreToken();

    Location location = currentLocation();
    IdentifierInfo *name = parseIdentifier();

    if (!name)
        return getInvalidNode();

    client.beginProcedureDeclaration(name, location);

    if (currentTokenIs(Lexer::TKN_LPAREN))
        parseSubroutineParameters();

    if (currentTokenIs(Lexer::TKN_RETURN)) {
        report(diag::RETURN_AFTER_PROCEDURE);
        seekTokens(Lexer::TKN_SEMI, Lexer::TKN_IS);
    }

    bool bodyFollows = currentTokenIs(Lexer::TKN_IS);

    // FIXME: We should model the parser state with more than a tag stack.
    if (bodyFollows)
        endTagStack.push(EndTagEntry(NAMED_TAG, location, name));

    return client.endSubroutineDeclaration(bodyFollows);
}

void Parser::parseSubroutineBody(Node declarationNode)
{
    Node context = client.beginSubroutineDefinition(declarationNode);

    while (!currentTokenIs(Lexer::TKN_BEGIN) &&
           !currentTokenIs(Lexer::TKN_EOT)) {

        // Check for the common error of only specifying a declarative part
        // without a body.
        if (currentTokenIs(Lexer::TKN_END)) {
            report(diag::UNEXPECTED_TOKEN_WANTED)
                << currentToken().getString()
                << Lexer::tokenString(Lexer::TKN_BEGIN);
            client.endSubroutineBody(context);
            goto PARSE_END_TAG;
        }

        parseDeclaration();
        requireToken(Lexer::TKN_SEMI);
    }

    requireToken(Lexer::TKN_BEGIN);

    while (!currentTokenIs(Lexer::TKN_END) &&
           !currentTokenIs(Lexer::TKN_EXCEPTION) &&
           !currentTokenIs(Lexer::TKN_EOT)) {
        Node stmt = parseStatement();
        if (stmt.isValid())
            client.acceptStmt(context, stmt);
    }

    // We are finished with the main body of the subroutine.  Inform the client.
    client.endSubroutineBody(context);

    // Parse any exception handlers.
    if (currentTokenIs(Lexer::TKN_EXCEPTION))
        parseExceptionStmt(context);

PARSE_END_TAG:
    EndTagEntry tagEntry = endTagStack.top();
    assert(tagEntry.kind == NAMED_TAG && "Inconsistent end tag stack!");

    endTagStack.pop();
    parseEndTag(tagEntry.tag);
    client.endSubroutineDefinition();
}

void Parser::parseFunctionDeclOrDefinition()
{
    Node decl = parseFunctionDeclaration();

    if (decl.isInvalid()) {
        seekTokens(Lexer::TKN_SEMI, Lexer::TKN_IS);
        if (currentTokenIs(Lexer::TKN_IS)) {
            EndTagEntry tagEntry = endTagStack.top();
            assert(tagEntry.kind == NAMED_TAG && "Inconsistent end tag stack!");
            endTagStack.pop();
            seekAndConsumeEndTag(tagEntry.tag);
        }
        return;
    }

    if (reduceToken(Lexer::TKN_IS))
        parseSubroutineBody(decl);
    return;
}

void Parser::parseProcedureDeclOrDefinition()
{
    Node decl = parseProcedureDeclaration();

    if (decl.isInvalid()) {
        seekTokens(Lexer::TKN_SEMI, Lexer::TKN_IS);
        if (currentTokenIs(Lexer::TKN_IS)) {
            EndTagEntry tagEntry = endTagStack.top();
            assert(tagEntry.kind == NAMED_TAG && "Inconsistent end tag stack!");
            endTagStack.pop();
            seekAndConsumeEndTag(tagEntry.tag);
        }
        return;
    }

    if (reduceToken(Lexer::TKN_IS))
        parseSubroutineBody(decl);
    return;
}

bool Parser::parseDeclaration()
{
    switch (currentTokenCode()) {
    default:
        report(diag::UNEXPECTED_TOKEN) << currentTokenString();
        seekToken(Lexer::TKN_SEMI);
        return false;

    case Lexer::TKN_IDENTIFIER:
        return parseObjectDeclaration();

    case Lexer::TKN_FUNCTION:
        return parseFunctionDeclaration().isValid();

    case Lexer::TKN_PROCEDURE:
        return parseProcedureDeclaration().isValid();

    case Lexer::TKN_IMPORT:
        return parseImportDeclaration();

    case Lexer::TKN_TYPE:
        return parseType();

    case Lexer::TKN_SUBTYPE:
        return parseSubtype();
    }
}

bool Parser::parseObjectDeclaration()
{
    IdentifierInfo *id;
    Location loc;

    assert(currentTokenIs(Lexer::TKN_IDENTIFIER));

    loc = currentLocation();
    id = parseIdentifier();

    if (!(id && requireToken(Lexer::TKN_COLON))) {
        seekAndConsumeToken(Lexer::TKN_SEMI);
        return false;
    }

    Node type = parseName();

    if (type.isValid()) {
        if (reduceToken(Lexer::TKN_RENAMES)) {
            Node target = parseName();
            if (target.isValid()) {
                client.acceptRenamedObjectDeclaration(loc, id, type, target);
                return true;
            }
        }
        else {
            Node init = getNullNode();
            if (reduceToken(Lexer::TKN_ASSIGN))
                init = parseExpr();
            if (init.isValid()) {
                client.acceptObjectDeclaration(loc, id, type, init);
                return true;
            }
        }
    }
    seekToken(Lexer::TKN_SEMI);
    return false;
}

bool Parser::parseImportDeclaration()
{
    assert(currentTokenIs(Lexer::TKN_IMPORT));
    ignoreToken();

    Node importedType = parseName();

    if (importedType.isValid()) {
        client.acceptImportDeclaration(importedType);
        return true;
    }
    return false;
}

bool Parser::parseType()
{
    assert(currentTokenIs(Lexer::TKN_TYPE));
    ignoreToken();

    Location loc = currentLocation();
    IdentifierInfo *name = parseIdentifier();

    if (!name)
        return false;

    // If we have a TKN_SEMI next on the stream accept an incomplete type
    // declaration.  Otherwise, ensure we a TKN_IS follows and consume it.
    if (currentTokenIs(Lexer::TKN_SEMI)) {
        client.acceptIncompleteTypeDecl(name, loc);
        return true;
    }
    else if (!requireToken(Lexer::TKN_IS))
        return false;

    // Determine what kind of type declaration this is.
    switch (currentTokenCode()) {

    default:
        report(diag::UNEXPECTED_TOKEN) << currentTokenString();
        seekSemi();
        break;

    case Lexer::TKN_LPAREN: {
        client.beginEnumeration(name, loc);
        parseEnumerationList();
        client.endEnumeration();
        return true;
    }

    case Lexer::TKN_RANGE:
        return parseIntegerRange(name, loc);

    case Lexer::TKN_ARRAY:
        return parseArrayTypeDecl(name, loc);

    case Lexer::TKN_RECORD:
    case Lexer::TKN_NULL:
        return parseRecordTypeDecl(name, loc);

    case Lexer::TKN_ACCESS:
        return parseAccessTypeDecl(name, loc);
    }

    return false;
}

bool Parser::parseSubtype()
{
    assert(currentTokenIs(Lexer::TKN_SUBTYPE));
    ignoreToken();

    Location loc = currentLocation();
    IdentifierInfo *name = parseIdentifier();

    if (!name || !requireToken(Lexer::TKN_IS)) {
        seekSemi();
        return false;
    }

    Node subtype = parseName();

    if (subtype.isInvalid()) {
        seekSemi();
        return false;
    }

    if (currentTokenIs(Lexer::TKN_SEMI)) {
        client.acceptSubtypeDecl(name, loc, subtype);
        return true;
    }

    // The only kind of subtype constraints we contend with at the moment are
    // range constraints.
    if (requireToken(Lexer::TKN_RANGE)) {
        Node low = parseExpr();
        if (low.isInvalid() or !requireToken(Lexer::TKN_DDOT)) {
            seekSemi();
            return false;
        }

        Node high = parseExpr();
        if (high.isInvalid()) {
            seekSemi();
            return false;
        }

        client.acceptRangedSubtypeDecl(name, loc, subtype, low, high);
        return true;
    }
    else {
        seekSemi();
        return false;
    }
}

void Parser::parseEnumerationList()
{
    assert(currentTokenIs(Lexer::TKN_LPAREN));
    Location loc = currentLocation();
    ignoreToken();

    // Diagnose empty enumeration lists.
    if (reduceToken(Lexer::TKN_RPAREN)) {
        report(loc, diag::EMPTY_ENUMERATION);
        return;
    }

    do {
        Location loc = currentLocation();
        if (currentTokenIs(Lexer::TKN_CHARACTER)) {
            IdentifierInfo *name = parseCharacter();
            client.acceptEnumerationCharacter(name, loc);
        }
        else {
            IdentifierInfo *name = parseIdentifier();

            if (!name) {
                seekCloseParen();
                return;
            }
            client.acceptEnumerationIdentifier(name, loc);
        }
    } while (reduceToken(Lexer::TKN_COMMA));

    requireToken(Lexer::TKN_RPAREN);
}

bool Parser::parseIntegerRange(IdentifierInfo *name, Location loc)
{
    assert(currentTokenIs(Lexer::TKN_RANGE));
    ignoreToken();

    Node low = parseExpr();
    if (low.isInvalid() or !requireToken(Lexer::TKN_DDOT)) {
        seekSemi();
        return false;
    }

    Node high = parseExpr();
    if (high.isInvalid()) {
        seekSemi();
        return false;
    }

    client.acceptIntegerTypeDecl(name, loc, low, high);
    return true;
}

void Parser::parseArrayIndexProfile(NodeVector &indices)
{
    assert(currentTokenIs(Lexer::TKN_LPAREN));
    ignoreToken();

    // Diagnose empty index profiles.
    if (reduceToken(Lexer::TKN_RPAREN)) {
        report(diag::EMPTY_ARRAY_TYPE_INDICES);
        return;
    }

    do {
        Node index = parseDSTDefinition(true);
        if (index.isValid())
            indices.push_back(index);
        else
            seekTokens(Lexer::TKN_COMMA, Lexer::TKN_RPAREN);
    } while (reduceToken(Lexer::TKN_COMMA));

    if (!requireToken(Lexer::TKN_RPAREN))
        seekCloseParen();
}

bool Parser::parseArrayTypeDecl(IdentifierInfo *name, Location loc)
{
    assert(currentTokenIs(Lexer::TKN_ARRAY));
    ignoreToken();

    if (!currentTokenIs(Lexer::TKN_LPAREN))
        return false;

    NodeVector indices;
    parseArrayIndexProfile(indices);

    if (indices.empty() || !requireToken(Lexer::TKN_OF)) {
        seekSemi();
        return false;
    }

    Node component = parseName();
    if (component.isInvalid()) {
        seekSemi();
        return false;
    }

    client.acceptArrayDecl(name, loc, indices, component);
    return true;
}

bool Parser::parseRecordTypeDecl(IdentifierInfo *name, Location loc)
{
    assert(currentTokenIs(Lexer::TKN_RECORD) ||
           currentTokenIs(Lexer::TKN_NULL));

    client.beginRecord(name, loc);

    if (currentTokenIs(Lexer::TKN_NULL) && nextTokenIs(Lexer::TKN_RECORD)) {
        ignoreToken();          // Ignore TKN_NULL.
        ignoreToken();          // Ignore TKN_RECORD.
        client.endRecord();
        return true;
    }
    else
        ignoreToken();          // Ignore TKN_RECORD.

    do {
        if (reduceToken(Lexer::TKN_NULL)) {
            requireToken(Lexer::TKN_SEMI);
            continue;
        }

        Location loc = currentLocation();
        IdentifierInfo *componentName = parseIdentifier();
        if (!componentName || !requireToken(Lexer::TKN_COLON))
            seekSemi();
        else {
            Node type = parseName();
            if (type.isValid())
                client.acceptRecordComponent(componentName, loc, type);
            else
                seekSemi();
        }

        requireToken(Lexer::TKN_SEMI);
    } while (!currentTokenIs(Lexer::TKN_END) &&
             !currentTokenIs(Lexer::TKN_EOT));

    client.endRecord();
    return requireToken(Lexer::TKN_END) && requireToken(Lexer::TKN_RECORD);
}

bool Parser::parseAccessTypeDecl(IdentifierInfo *name, Location loc)
{
    assert(currentTokenIs(Lexer::TKN_ACCESS));
    ignoreToken();

    Node subtypeNode = parseName();

    if (subtypeNode.isInvalid())
        return false;

    client.acceptAccessTypeDecl(name, loc, subtypeNode);
    return true;
}

bool Parser::parseTopLevelDeclaration()
{
    for (;;) {
        switch (currentTokenCode()) {
        case Lexer::TKN_PACKAGE:
        case Lexer::TKN_SIGNATURE:
        case Lexer::TKN_DOMAIN:
        case Lexer::TKN_GENERIC:
            parseCapsule();
            return true;

        case Lexer::TKN_EOT:
            return false;
            break;

        default:
            // An invalid token was found at top level. Do not try to recover.
            report(diag::UNEXPECTED_TOKEN) << currentToken().getString();
            return false;
        }
    }
}

void Parser::parseCompilationUnit()
{
PARSE_CONTEXT:
    switch (currentTokenCode()) {
    default:
        break;

    case Lexer::TKN_WITH:
        parseWithClause();
        goto PARSE_CONTEXT;

    case Lexer::TKN_EOT:
        return;
    }

    while (parseTopLevelDeclaration()) { }
}

// Converts a character array representing a Comma integer literal into an
// llvm::APInt.  The bit width of the resulting APInt is always set to the
// minimal number of bits needed to represent the given number.
void Parser::decimalLiteralToAPInt(const char *start, unsigned length,
                                   llvm::APInt &value)
{
    std::string digits;
    for (const char *cursor = start; cursor != start + length; ++cursor) {
        char ch = *cursor;
        if (ch != '_')
            digits.push_back(ch);
    }
    assert(!digits.empty() && "Empty string literal!");

    // Get the binary value and adjust the number of bits to an accurate width.
    unsigned numBits = llvm::APInt::getBitsNeeded(digits, 10);
    value = llvm::APInt(numBits, digits, 10);
    if (value == 0)
        numBits = 1;
    else
        numBits = value.getActiveBits();
    value.zextOrTrunc(numBits);
}

void Parser::parseDeclarationPragma()
{
    assert(currentTokenIs(Lexer::TKN_PRAGMA));
    ignoreToken();

    Location loc = currentLocation();
    IdentifierInfo *name = parseIdentifier();

    if (!name)
        return;

    llvm::StringRef ref(name->getString());
    pragma::PragmaID ID = pragma::getPragmaID(ref);

    if (ID == pragma::UNKNOWN_PRAGMA) {
        report(loc, diag::UNKNOWN_PRAGMA) << name;
        return;
    }

    // Currently, the only pragma accepted in a declaration context is Import.
    // When the set of valid pragmas expands, special parsers will be written to
    // parse the arguments.
    switch (ID) {
    default:
        report(loc, diag::INVALID_PRAGMA_CONTEXT) << name;
        break;

    case pragma::Import:
        parsePragmaImport(loc);
        break;
    }
}

void Parser::parsePragmaImport(Location pragmaLoc)
{
    if (!requireToken(Lexer::TKN_LPAREN))
        return;

    // The first argument is an identifier naming the import convention.  The
    // parser does not know anything about convention names.
    Location conventionLoc = currentLocation();
    IdentifierInfo *conventionName = parseIdentifier();
    if (!conventionName || !requireToken(Lexer::TKN_COMMA)) {
        seekCloseParen();
        return;
    }

    // The second argument is the name of the local declaration corresponding to
    // the imported entity.
    Location entityLoc = currentLocation();
    IdentifierInfo *entityName = parseFunctionIdentifier();
    if (!entityName || !requireToken(Lexer::TKN_COMMA)) {
        seekCloseParen();
        return;
    }

    // Finally, the external name.  This is a general expression.
    Node externalName = parseExpr();
    if (externalName.isInvalid() || !requireToken(Lexer::TKN_RPAREN)) {
        seekCloseParen();
        return;
    }

    client.acceptPragmaImport(pragmaLoc,
                              conventionName, conventionLoc,
                              entityName, entityLoc, externalName);
}

Node Parser::parseDSTDefinition(bool acceptDiamond)
{
    // We are always called to parse the control of a for statement or an array
    // index specification.  We need to distinguish between names which denote
    // subtype marks and simple ranges.  Use use a knowledge of our context to
    // determine the difference.
    //
    // Specultively parse a name.  If the parse suceeds look at the following
    // token.  If it is TKN_RANGE then we definitely have a subtype mark.  If we
    // were called from a loop context we could also have TKN_LOOP.  If we were
    // called from an array index context we could have TKN_COMMA or TKN_RPAREN.
    //
    // An alternative strategy would be to parse a name and look for infix
    // operators and TKN_DDOT.
    bool rangeFollows = false;
    beginExcursion();

    if (consumeName()) {
        switch (currentTokenCode()) {
        default:
            rangeFollows = true;
            break;
        case Lexer::TKN_RANGE:
        case Lexer::TKN_LOOP:
        case Lexer::TKN_COMMA:
        case Lexer::TKN_RPAREN:
            rangeFollows = false;
            break;
        }
    }
    else
        rangeFollows = true;

    endExcursion();

    if (rangeFollows) {
        // FIXME: Should be parsing simple expressions here.
        Node lower = parseExpr();
        if (lower.isInvalid() || !requireToken(Lexer::TKN_DDOT))
            return getInvalidNode();

        Node upper = parseExpr();
        if (upper.isInvalid())
            return getInvalidNode();
        else
            return client.acceptDSTDefinition(lower, upper);
    }

    Node name = parseName(Accept_Range_Attribute);
    if (name.isInvalid())
        return getInvalidNode();

    if (reduceToken(Lexer::TKN_RANGE)) {
        if (currentTokenIs(Lexer::TKN_DIAMOND)) {
            Location loc = ignoreToken();
            if (acceptDiamond)
                return client.acceptDSTDefinition(name, true);
            else {
                report(loc, diag::UNEXPECTED_TOKEN) <<
                    Lexer::tokenString(Lexer::TKN_DIAMOND);
                return getInvalidNode();
            }
        }

        // FIXME: We should parse simple expressions here.
        Node lower = parseExpr();

        if (lower.isInvalid() || !requireToken(Lexer::TKN_DDOT))
            return getInvalidNode();

        Node upper = parseExpr();
        if (upper.isInvalid())
            return getInvalidNode();

        return client.acceptDSTDefinition(name, lower, upper);
    }
    else
        return client.acceptDSTDefinition(name, false);
}
