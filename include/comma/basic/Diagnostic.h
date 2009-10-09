//===-- basic/Diagnostic.h ------------------------------------ -*- C++ -*-===//
//
// This file is distributed under the MIT license.  See LICENSE.txt for details.
//
// Copyright (C) 2008-2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#ifndef COMMA_BASIC_DIAGNOSTIC_HDR_GUARD
#define COMMA_BASIC_DIAGNOSTIC_HDR_GUARD

#include "comma/basic/IdentifierInfo.h"
#include "comma/basic/Location.h"
#include "comma/basic/ParameterModes.h"
#include <iostream>
#include <sstream>

namespace comma {

namespace diag {

enum Kind {
#define DIAGNOSTIC(ENUM, KIND, FORMAT) ENUM,
#include "comma/basic/Diagnostic.def"
#undef DIAGNOSTIC

    LAST_UNUSED_DIAGNOSTIC_KIND
};

} // End diag namespace.


class DiagnosticStream {

public:
    DiagnosticStream(std::ostream &stream);

    ~DiagnosticStream();

    DiagnosticStream &initialize(const SourceLocation &sloc, const char *format);

    DiagnosticStream &operator<<(const std::string &string);

    DiagnosticStream &operator<<(const char *string);

    DiagnosticStream &operator<<(int n);

    DiagnosticStream &operator<<(char c);

    DiagnosticStream &operator<<(const SourceLocation &sloc);

    DiagnosticStream &operator<<(const IdentifierInfo *idInfo);

    DiagnosticStream &operator<<(PM::ParameterMode mode);

private:
    void emitFormatComponent();

    void emitSourceLocation(const SourceLocation &sloc);

    std::ostream       &stream;
    unsigned            position;
    std::ostringstream  message;
    const char         *format;
    SourceLocation      sourceLoc;
};

class Diagnostic {

public:
    // Creates a diagnostic object with the reporting stream defaulting to
    // std::cerr;
    Diagnostic() : diagstream(std::cerr), reportCount(0) { }

    // Creates a diagnostic object with the given output stream serving as the
    // default stream to which messages are delivered.
    Diagnostic(std::ostream &stream) : diagstream(stream) { }

    DiagnosticStream &report(const SourceLocation &loc, diag::Kind kind);

    const char *getDiagnosticFormat(diag::Kind);

    // Returns true if report() has been called.
    bool reportsGenerated() { return reportCount != 0; }

    // Returns the number of reports handled by this Diagnostic so far.
    unsigned numReports() const { return reportCount; }

private:
    DiagnosticStream diagstream;
    unsigned reportCount;

    static void initializeMessages();

    static const char *messages[diag::LAST_UNUSED_DIAGNOSTIC_KIND];
};

} // End comma namespace.

#endif
