//===-- parser/Lexer.cpp -------------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008-2010, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "comma/parser/Lexer.h"

#include <cstring>

using namespace comma;

Lexer::Lexer(TextProvider &txtProvider, Diagnostic &diag)
    : txtProvider(txtProvider),
      diagnostic(diag),
      currentIter(txtProvider.begin()),
      errorCount(0),
      scanningAborted(false),
      index(0)
{ }

llvm::StringRef Lexer::Token::getString() const
{
    return Lexer::tokenString(*this);
}

llvm::StringRef Lexer::tokenString(const Code code)
{
    const char *result;

    switch (code) {
    default:
        result = 0;
        break;

#define RESERVED(NAME, STRING) case TKN_ ## NAME: result = STRING; break;
#define GLYPH(NAME, STRING)    case TKN_ ## NAME: result = STRING; break;
#include "comma/parser/Tokens.def"
#undef RESERVED
#undef GLYPH
    }

    return result;
}

llvm::StringRef Lexer::tokenString(const Token &token)
{
    Code code = token.getCode();

    switch (code) {
    default:
        return tokenString(code);
        break;

    case TKN_ATTRIBUTE:
    case TKN_IDENTIFIER:
    case TKN_INTEGER:
    case TKN_REAL:
    case TKN_STRING:
    case TKN_CHARACTER:
        return token.getRep();
    }
}

bool Lexer::isDecimalDigit(unsigned c)
{
    return ('0' <= c && c <= '9');
}

bool Lexer::isInitialIdentifierChar(unsigned c)
{
    if (('a' <= c && c <= 'z') ||
        ('A' <= c && c <= 'Z') ||
        (c == '%') || (c == '_'))
        return true;

    return false;
}

bool Lexer::isInnerIdentifierChar(unsigned c)
{
    return isInitialIdentifierChar(c) || isDecimalDigit(c) || c == '?';
}

bool Lexer::isWhitespace(unsigned c)
{
    return (c == ' ') || (c == '\t') || (c == '\n');
}

Location Lexer::currentLocation() const
{
    return txtProvider.getLocation(currentIter);
}

// Something of a fundamental function, since all characters are gathered from
// the underlying stream via this routine.
unsigned Lexer::readStream()
{
    unsigned c = *currentIter;
    ++currentIter;

    // Ensure that carriage returns and DOS style newline sequences are
    // canonicalized into single newline character codes.
    switch (c) {

    case '\r':
        if (*currentIter == '\n')
            ++currentIter;
    case '\n':
        return '\n';
    }

    return c;
}

unsigned Lexer::peekStream()
{
    unsigned c = *currentIter;

    if (c == '\r')
        return '\n';

    return c;
}

void Lexer::ungetStream()
{
    --currentIter;
}

void Lexer::ignoreStream()
{
    readStream();
}

bool Lexer::eatComment()
{
    unsigned c = peekStream();

    if (c == '-') {
        ignoreStream();
        if (peekStream() == '-') {
            // Loop until either a newline or the input stream is
            // exhausted.
            for (;;) {
                c = readStream();
                if (c == '\n' || c == 0)
                    return true;
            }
        }
        else {
            ungetStream();
            return false;
        }
    }
    return false;
}

bool Lexer::eatWhitespace()
{
    unsigned c = peekStream();

    if (isWhitespace(c)) {
        do {
            ignoreStream();
        } while (isWhitespace(c = peekStream()));
        return true;
    }
    return false;
}

void Lexer::emitToken(Code code,
                      const TextIterator &start, const TextIterator &end)
{
    Location    loc    = txtProvider.getLocation(start);
    const char *string = &start;
    unsigned    length = &end - &start;
    *targetToken = Token(code, loc, llvm::StringRef(string, length));
}

void Lexer::emitToken(Code code, Location loc)
{
    *targetToken = Token(code, loc, llvm::StringRef(0, 0));
}

void Lexer::emitStringToken(const TextIterator &start, const TextIterator &end)
{
    emitToken(TKN_STRING, start, end);
}

void Lexer::emitIntegerToken(const TextIterator &start, const TextIterator &end)
{
    emitToken(TKN_INTEGER, start, end);
}

void Lexer::emitRealToken(const TextIterator &start, const TextIterator &end)
{
    emitToken(TKN_REAL, start, end);
}

void Lexer::emitIdentifierToken(Location loc)
{
    *targetToken = Token(TKN_IDENTIFIER, loc, nameBuff.str());
}

void Lexer::emitAttributeToken(Location loc)
{
    *targetToken = Token(TKN_ATTRIBUTE, loc, nameBuff.str());
}

void Lexer::emitCharacterToken(const TextIterator &start, const TextIterator &end)
{
    emitToken(TKN_CHARACTER, start, end);
}

Lexer::Code Lexer::getTokenCode() const
{
    Code code = UNUSED_ID;
    const char *str = &nameBuff[0];
    unsigned length = nameBuff.size();

    switch (length) {
    case 1:
        if (strncmp(str, "%", length) == 0)
            code = TKN_PERCENT;
        break;

    case 2:
        if (strncmp(str, "is", length) == 0)
            code = TKN_IS;
        else if (strncmp(str, "if", length) == 0)
            code = TKN_IF;
        else if (strncmp(str, "in", length) == 0)
            code = TKN_IN;
        else if (strncmp(str, "of", length) == 0)
            code = TKN_OF;
        else if (strncmp(str, "or", length) == 0)
            code = TKN_OR;
        break;

    case 3:
        if (strncmp(str, "end", length) == 0)
            code = TKN_END;
        else if (strncmp(str, "out", length) == 0)
            code = TKN_OUT;
        else if (strncmp(str, "and", length) == 0)
            code = TKN_AND;
        else if (strncmp(str, "mod", length) == 0)
            code = TKN_MOD;
        else if (strncmp(str, "rem", length) == 0)
            code = TKN_REM;
        else if (strncmp(str, "for", length) == 0)
            code = TKN_FOR;
        else if (strncmp(str, "not", length) == 0)
            code = TKN_NOT;
        else if (strncmp(str, "xor", length) == 0)
            code = TKN_XOR;
        else if (strncmp(str, "new", length) == 0)
            code = TKN_NEW;
        else if (strncmp(str, "all", length) == 0)
            code = TKN_ALL;
        else if (strncmp(str, "use", length) == 0)
            code = TKN_USE;
        break;

    case 4:
        if (strncmp(str, "else", length) == 0)
            code = TKN_ELSE;
        else if (strncmp(str, "loop", length) == 0)
            code = TKN_LOOP;
        else if (strncmp(str, "then", length) == 0)
            code = TKN_THEN;
        else if (strncmp(str, "with", length) == 0)
            code = TKN_WITH;
        else if (strncmp(str, "type", length) == 0)
            code = TKN_TYPE;
        else if (strncmp(str, "when", length) == 0)
            code = TKN_WHEN;
        else if (strncmp(str, "null", length) == 0)
            code = TKN_NULL;
        else if (strncmp(str, "exit", length) == 0)
            code = TKN_EXIT;
        else if (strncmp(str, "body", length) == 0)
            code = TKN_BODY;
        break;

    case 5:
        if (strncmp(str, "begin", length) == 0)
            code = TKN_BEGIN;
        else if (strncmp(str, "elsif", length) == 0)
            code = TKN_ELSIF;
        else if (strncmp(str, "while", length) == 0)
            code = TKN_WHILE;
        else if (strncmp(str, "range", length) == 0)
            code = TKN_RANGE;
        else if (strncmp(str, "array", length) == 0)
            code = TKN_ARRAY;
        else if (strncmp(str, "raise", length) == 0)
            code = TKN_RAISE;
        break;

    case 6:
        if (strncmp(str, "return", length) == 0)
            code = TKN_RETURN;
        else if (strncmp(str, "pragma", length) == 0)
            code = TKN_PRAGMA;
        else if (strncmp(str, "others", length) == 0)
            code = TKN_OTHERS;
        else if (strncmp(str, "record", length) == 0)
            code = TKN_RECORD;
        else if (strncmp(str, "access", length) == 0)
            code = TKN_ACCESS;
        else if (strncmp(str, "digits", length) == 0)
            code = TKN_DIGITS;
        break;

    case 7:
        if (strncmp(str, "declare", length) == 0)
            code = TKN_DECLARE;
        else if (strncmp(str, "generic", length) == 0)
            code = TKN_GENERIC;
        else if (strncmp(str, "package", length) == 0)
            code = TKN_PACKAGE;
        else if (strncmp(str, "subtype", length) == 0)
            code = TKN_SUBTYPE;
        else if (strncmp(str, "reverse", length) == 0)
            code = TKN_REVERSE;
        else if (strncmp(str, "renames", length) == 0)
            code = TKN_RENAMES;
        else if (strncmp(str, "private", length) == 0)
            code = TKN_PRIVATE;
        else if (strncmp(str, "limited", length) == 0)
            code = TKN_LIMITED;
        break;

    case 8:
        if (strncmp(str, "function", length) == 0)
            code = TKN_FUNCTION;
        else if (strncmp(str, "abstract", length) == 0)
            code = TKN_ABSTRACT;
        break;

    case 9:
        if (strncmp(str, "procedure", length) == 0)
            code = TKN_PROCEDURE;
        else if (strncmp(str, "exception", length) == 0)
            code = TKN_EXCEPTION;
        break;
    }
    return code;
}

void Lexer::diagnoseConsecutiveUnderscores(unsigned c1, unsigned c2)
{
    if (c1 == '_' && c2 == '_') {
        report(diag::CONSECUTIVE_UNDERSCORE);
        do {
            ignoreStream();
        } while ((c2 = peekStream()) == '_');
    }
}

bool Lexer::consumeName()
{
    unsigned c1, c2;

    if (isInitialIdentifierChar(c1 = peekStream())) {
        do {
            nameBuff.push_back(std::tolower(c1));
            ignoreStream();
            c2 = peekStream();
            diagnoseConsecutiveUnderscores(c1, c2);
        } while (isInnerIdentifierChar(c1 = c2));

        return true;
    }
    return false;
}

bool Lexer::scanName()
{
    Location loc = currentLocation();

    if (consumeName()) {
        Code code = getTokenCode();

        if (code == UNUSED_ID)
            emitIdentifierToken(loc);
        else
            emitToken(code, loc);
        nameBuff.clear();
        return true;
    }
    return false;
}

void Lexer::scanAttribute(Location loc)
{
    TextIterator start = currentIter;

    if (!consumeName())
        report(loc, diag::INVALID_ATTRIBUTE);

    emitAttributeToken(loc);
    nameBuff.clear();
}

bool Lexer::scanGlyph()
{
    Location loc = currentLocation();
    unsigned c = readStream();
    Code code  = UNUSED_ID;

    switch (c) {
    case '(':
        code = TKN_LPAREN;
        break;

    case ')':
        code = TKN_RPAREN;
        break;

    case ';':
        code = TKN_SEMI;
        break;

    case '.':
        switch (peekStream()) {
        case '.':
            ignoreStream();
            code = TKN_DDOT;
            break;

        default:
            code = TKN_DOT;
        }
        break;

    case ':':
        switch (peekStream()) {
        case '=':
            ignoreStream();
            code = TKN_ASSIGN;
            break;

        default:
            code = TKN_COLON;
        }
        break;

    case ',':
        code = TKN_COMMA;
        break;

    case '=':
        switch (peekStream()) {
        default:
            code = TKN_EQUAL;
            break;

        case '>':
            ignoreStream();
            code = TKN_RDARROW;
            break;
        }
        break;

    case '<':
        switch (peekStream()) {
        default:
            code = TKN_LESS;
            break;

        case '=':
            ignoreStream();
            code = TKN_LEQ;
            break;

        case '>':
            ignoreStream();
            code = TKN_DIAMOND;
        }
        break;

    case '>':
        switch (peekStream()) {
        default:
            code = TKN_GREAT;
            break;

        case '=':
            ignoreStream();
            code = TKN_GEQ;
            break;
        }
        break;

    case '+':
        code = TKN_PLUS;
        break;

    case '-':
        code = TKN_MINUS;
        break;

    case '*':
        switch (peekStream()) {
        case '*':
            ignoreStream();
            code = TKN_POW;
            break;

        default:
            code = TKN_STAR;
        }
        break;

    case '/':
        switch (peekStream()) {
        case '=':
            ignoreStream();
            code = TKN_NEQUAL;
            break;

        default:
            code = TKN_FSLASH;
        }
        break;

    case '&':
        code = TKN_AMPER;
        break;

    case '|':
        code = TKN_BAR;
        break;
    }

    if (code == UNUSED_ID) {
        ungetStream();
        return false;
    }

    emitToken(code, loc);
    return true;
}

bool Lexer::scanEscape()
{
    Location loc = currentLocation();
    unsigned c;

    switch (c = readStream()) {
    case '\\': break;
    case '"' : break;
    case '\'': break;
    case 't' : break;
    case 'n' : break;
    case 'r' : break;
    case 'b' : break;

    case 0:
        // Premature end of stream.  We let this condition be picked up by the
        // caller.
        ungetStream();
        return false;

    default:
        // Illegal escape sequence.
        report(loc, diag::ILLEGAL_ESCAPE) << (char)c;
        return false;
    }
    return true;
}

bool Lexer::scanCharacter()
{
    TextIterator start = currentIter;
    Location loc = currentLocation();
    unsigned c;

    if (peekStream() == '\'') {
        ignoreStream();
        c = readStream();

        // Check for empty enumeration literal.  This is not valid.  Consume and
        // report.
        if (c == '\'') {
            report(loc, diag::EMPTY_CHARACTER_LITERAL);
            emitCharacterToken(start, currentIter);
            return true;
        }

        // If we do not have a terminating quote there are two valid options.
        // Either we have a qualified expression (a quote-left-pren), or an
        // attribute.
        if (peekStream() != '\'') {
            ungetStream();
            if (peekStream() == '(')
                emitToken(TKN_QUOTE, loc);
            else
                scanAttribute(loc);
            return true;
        }

        // Special case for the character representing a left paren.  We need to
        // deal with qualified expressions containing a character.  Take
        // "Character'('x')" or "String'('x', 'y')" for example.  We handle this
        // oddball case by checking if another quote is two characters away.
        if (c == '(') {
            TextIterator cursor = currentIter;
            if (*++cursor && *++cursor == '\'') {
                ungetStream();
                emitToken(TKN_QUOTE, loc);
                return true;
            }
        }

        // Otherwise we have a character literal.
        //
        // FIXME: Ensure the character belongs to the standard character set.
        ignoreStream();
        emitCharacterToken(start, currentIter);
        return true;
    }
    return false;
}

bool Lexer::scanString()
{
    TextIterator start = currentIter;
    Location loc = currentLocation();
    unsigned c;

    if (peekStream() == '"') {
        ignoreStream();

        for (;;) {
            switch (c = readStream()) {
            case '\\':
                // Note that if scanning of the escape fails, we simply do not
                // accumulate the offending sequence and continue scanning.
                scanEscape();
                break;

            case 0:
                // Premature end of stream.  Form the string literal from all
                // tokens accumulated thus far.
                report(loc, diag::UNTERMINATED_STRING);
                emitStringToken(start, currentIter);
                return true;

            case '\n':
                // Embedded newline.
                report(loc, diag::NEWLINE_IN_STRING_LIT);
                emitStringToken(start, currentIter);
                return true;

            case '"':
                // End of string literal.
                emitStringToken(start, currentIter);
                return true;
            }
        }
    }
    return false;
}

bool Lexer::scanNumeric()
{
    Location loc = currentLocation();
    TextIterator start = currentIter;
    bool isReal = false;
    unsigned c = peekStream();

    if (!isDecimalDigit(c))
        return false;

    ignoreStream();

    // Decimal literals cannot have a leading zero (except for the zero
    // literal, of course).  When we spot such a malformed integer, emit a
    // diagnostic and drop the leading zeros.
    if (c == '0' && isDecimalDigit(peekStream())) {
        report(loc, diag::LEADING_ZERO_IN_INTEGER_LIT);

        while (peekStream() == '0') ignoreStream();

        // Check if we have a string of zeros.  Simply return the zero token
        // in such a case.  Otherwise, continue scanning normally.
        if (!isDecimalDigit(peekStream())) {
            TextIterator end = start;
            emitIntegerToken(start, ++end);
            return true;
        }
        else
            c = readStream();
    }

    for (;;) {
        c = readStream();

        if (isDecimalDigit(c) || c == '_')
            continue;

        // Check for decimal point.
        if (c == '.') {

            // Check for '..'.
            if (peekStream() == '.') {
                ungetStream();
                break;
            }

            // Ensure there are not multiple decimal points in this literal.
            if (isReal) {
                report(loc, diag::MALFORMED_NUMERIC_LITERAL);
                ungetStream();
                emitRealToken(start, currentIter);
                return true;
            }

            // If the decimal point is not followed by a digit return an
            // integer token.
            if (!isDecimalDigit(peekStream())) {
                report(loc, diag::MALFORMED_NUMERIC_LITERAL);
                TextIterator end = currentIter;
                emitIntegerToken(start, --end);
                return true;
            }

            isReal = true;
            continue;
        }

        // Scan past any exponent part plus optional sign.
        if (c == 'E' || c == 'e') {

            // Save the text iterator pointing at the exponent and back up to
            // the last valid decimal digit.
            TextIterator end = currentIter;
            --end;

            c = peekStream();

            if (c == '-') {
                // Integer literals may not have a negative exponent.  In this
                // case we exclude the exponent part of the literal from the
                // token.
                if (!isReal) {
                    report(loc, diag::NEGATIVE_EXPONENT_IN_INTEGER_LITERAL);
                    do {
                        ignoreStream();
                        c = peekStream();
                    } while (isDecimalDigit(c) || c == '_');
                    emitIntegerToken(start, end);
                    return true;
                }

                ignoreStream();
                c = peekStream();
            }
            else if (c == '+') {
                ignoreStream();
                c = peekStream();
            }

            // The exponent and optional sign must be followed by a digit.  If
            // we do not have a well formed exponent emit the literal without
            // it.
            if (!isDecimalDigit(c)) {
                report(loc, diag::MALFORMED_NUMERIC_LITERAL);
                if (isReal)
                    emitRealToken(start, end);
                else
                    emitIntegerToken(start, end);
                return true;
            }

            continue;
        }

        // The current character does not contribute to the literal.
        ungetStream();
        break;
    }

    if (isReal)
        emitRealToken(start, currentIter);
    else
        emitIntegerToken(start, currentIter);

    return true;
}

void Lexer::beginExcursion()
{
    positionStack.push_back(index);
}

void Lexer::endExcursion()
{
    index = positionStack.back();
    positionStack.pop_back();
}

void Lexer::forgetExcursion()
{
    unsigned saved_index = positionStack.back();
    positionStack.pop_back();

    if (positionStack.empty()) {
        assert(saved_index == 0 && "index/position mismatch!");
        ((void)saved_index);
        tokens.clear();
    }
}

void Lexer::peek(Token &tkn, unsigned n)
{
    unsigned numTokens = tokens.size();

    if (index + n < numTokens) {
        tkn = tokens[index + n];
        return;
    }

    unsigned tokensNeeded = index + n - numTokens;
    targetToken = &tkn;
    for (unsigned i = 0; i <= tokensNeeded; ++i) {
        scanToken();
        if (targetToken->getCode() != TKN_EOT)
            tokens.push_back(*targetToken);
    }
}

void Lexer::scan(Token &tkn)
{
    unsigned numTokens = tokens.size();

    // Check if we have a cached token to return.
    if (index < numTokens) {
        tkn = tokens[index++];
        return;
    }

    // Clear the token buffer if it is not empty and we are not in an excursion.
    if (numTokens && positionStack.empty()) {
        tokens.clear();
        index = 0;
    }

    targetToken = &tkn;

    scanToken();

    // Save the token if we are in an excursion and it is not EOT.
    if (!positionStack.empty() && targetToken->getCode() != TKN_EOT) {
        index++;
        tokens.push_back(*targetToken);
    }
}

void Lexer::scanToken()
{
    for (;;) {
        eatWhitespace();
        while (eatComment()) eatWhitespace();

        if (peekStream() == 0 || scanningAborted) {
            emitToken(TKN_EOT, Location());
            return;
        }

        if (scanName())      return;
        if (scanGlyph())     return;
        if (scanString())    return;
        if (scanNumeric())   return;
        if (scanCharacter()) return;

        // For invalid character data emit a diagnostic and abort the scan.
        //
        // FIXME: There should be an isSourceChar function to check if the
        // character belongs to the source character set.  Scanning could just
        // skip legal characters.  Characters which do not fall into the
        // expected character set should likely have their hex value printed.
        report(diag::INVALID_CHARACTER) << static_cast<char>(peekStream());
        ignoreStream();
        abortScanning();
    }
}

