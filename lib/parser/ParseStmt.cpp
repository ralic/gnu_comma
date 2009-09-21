//===-- parser/ParseStmt.cpp ---------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008-2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "comma/parser/Parser.h"

#include <cassert>
#include <cstring>

using namespace comma;

Node Parser::parseStatement()
{
    Node node = getInvalidNode();

    switch (currentTokenCode()) {

    default:
        if (assignmentFollows())
            node = parseAssignmentStmt();
        else if (blockStmtFollows())
            node = parseBlockStmt();
        else
            node = parseProcedureCallStatement();
        break;

    case Lexer::TKN_IF:
        node = parseIfStmt();
        break;

    case Lexer::TKN_WHILE:
        node = parseWhileStmt();
        break;

    case Lexer::TKN_RETURN:
        node = parseReturnStmt();
        break;

    case Lexer::TKN_PRAGMA:
        node = parsePragmaStmt();
        break;
    }

    if (node.isInvalid() || !requireToken(Lexer::TKN_SEMI))
        seekAndConsumeToken(Lexer::TKN_SEMI);

    return node;
}

Node Parser::parseProcedureCallStatement()
{
    Node name = parseName(true);
    if (name.isValid())
        return client.acceptProcedureCall(name);
    return getInvalidNode();
}

Node Parser::parseReturnStmt()
{
    assert(currentTokenIs(Lexer::TKN_RETURN));

    Location loc = ignoreToken();

    if (currentTokenIs(Lexer::TKN_SEMI))
        return client.acceptEmptyReturnStmt(loc);

    Node expr = parseExpr();

    if (expr.isValid())
        return client.acceptReturnStmt(loc, expr);
    return expr;
}

Node Parser::parseAssignmentStmt()
{
    assert(assignmentFollows());

    Node target = parseName(false);

    if (target.isInvalid())
        return getInvalidNode();

    ignoreToken();              // Ignore the `:='.

    Node value = parseExpr();

    if (value.isValid())
        return client.acceptAssignmentStmt(target, value);

    return getInvalidNode();
}

Node Parser::parseIfStmt()
{
    assert(currentTokenIs(Lexer::TKN_IF));

    Location   loc       = ignoreToken();
    Node       condition = parseExpr();
    NodeVector stmts;

    if (condition.isInvalid() || !requireToken(Lexer::TKN_THEN)) {
        seekEndIf();
        return getInvalidNode();
    }

    do {
        Node stmt = parseStatement();
        if (stmt.isValid())
            stmts.push_back(stmt);
    } while (!currentTokenIs(Lexer::TKN_END)   &&
             !currentTokenIs(Lexer::TKN_ELSE)  &&
             !currentTokenIs(Lexer::TKN_ELSIF) &&
             !currentTokenIs(Lexer::TKN_EOT));

    Node result = client.acceptIfStmt(loc, condition, stmts);
    if (result.isInvalid()) {
        seekEndIf();
        return getInvalidNode();
    }

    while (currentTokenIs(Lexer::TKN_ELSIF)) {
        loc       = ignoreToken();
        condition = parseExpr();
        if (condition.isInvalid() || !requireToken(Lexer::TKN_THEN)) {
            seekEndIf();
            return getInvalidNode();
        }

        stmts.clear();
        do {
            Node stmt = parseStatement();
            if (stmt.isValid())
                stmts.push_back(stmt);
        } while (!currentTokenIs(Lexer::TKN_END) &&
                 !currentTokenIs(Lexer::TKN_ELSE) &&
                 !currentTokenIs(Lexer::TKN_ELSIF) &&
                 !currentTokenIs(Lexer::TKN_EOT));

        result = client.acceptElsifStmt(loc, result, condition, stmts);

        if (result.isInvalid()) {
            seekEndIf();
            return getInvalidNode();
        }
    }

    if (currentTokenIs(Lexer::TKN_ELSE)) {
        loc = ignoreToken();
        stmts.clear();
        do {
            Node stmt = parseStatement();
            if (stmt.isValid())
                stmts.push_back(stmt);
        } while (!currentTokenIs(Lexer::TKN_END)  &&
                 !currentTokenIs(Lexer::TKN_EOT));

        result = client.acceptElseStmt(loc, result, stmts);
    }

    if (!requireToken(Lexer::TKN_END) || !requireToken(Lexer::TKN_IF))
        return getInvalidNode();

    return Node(result);
}

Node Parser::parseBlockStmt()
{
    Location        loc   = currentLocation();
    IdentifierInfo *label = 0;

    assert(blockStmtFollows());

    // Parse this blocks label, if available.
    if (currentTokenIs(Lexer::TKN_IDENTIFIER)) {
        label = parseIdentifierInfo();
        ignoreToken();          // Ignore the ":".
    }

    Node block = client.beginBlockStmt(loc, label);

    if (reduceToken(Lexer::TKN_DECLARE)) {
        while (!currentTokenIs(Lexer::TKN_BEGIN) &&
               !currentTokenIs(Lexer::TKN_EOT)) {
            parseDeclaration();
            requireToken(Lexer::TKN_SEMI);
        }
    }

    if (requireToken(Lexer::TKN_BEGIN)) {
        while (!currentTokenIs(Lexer::TKN_END) &&
               !currentTokenIs(Lexer::TKN_EOT)) {
            Node stmt = parseStatement();
            if (stmt.isValid())
                client.acceptBlockStmt(block, stmt);
        }
        if (parseEndTag(label)) {
            client.endBlockStmt(block);
            return block;
        }
    }

    seekAndConsumeEndTag(label);
    return getInvalidNode();
}

Node Parser::parseWhileStmt()
{
    assert(currentTokenIs(Lexer::TKN_WHILE));

    Location loc = ignoreToken();
    Node condition = parseExpr();
    NodeVector stmts;

    if (condition.isInvalid() || !requireToken(Lexer::TKN_LOOP)) {
        seekEndLoop();
        return getInvalidNode();
    }

    do {
        Node stmt = parseStatement();
        if (stmt.isValid())
            stmts.push_back(stmt);
    } while (!currentTokenIs(Lexer::TKN_END) and
             !currentTokenIs(Lexer::TKN_EOT));

    if (!requireToken(Lexer::TKN_END) or !requireToken(Lexer::TKN_LOOP))
        return getInvalidNode();

    return client.acceptWhileStmt(loc, condition, stmts);
}

Node Parser::parsePragmaStmt()
{
    assert(currentTokenIs(Lexer::TKN_PRAGMA));
    ignoreToken();

    Location loc = currentLocation();
    IdentifierInfo *name = parseIdentifierInfo();
    NodeVector args;

    if (!name)
        return getInvalidNode();

    // The only kind of pragma we currently support are Assert pragmas.
    if (std::strcmp(name->getString(), "Assert") == 0) {
        if (!requireToken(Lexer::TKN_LPAREN))
            return getInvalidNode();

        Node condition = parseExpr();

        if (condition.isInvalid() || !requireToken(Lexer::TKN_RPAREN)) {
            seekTokens(Lexer::TKN_RPAREN);
            return getInvalidNode();
        }

        args.push_back(condition);
    }

    return client.acceptPragmaStmt(name, loc, args);
}
