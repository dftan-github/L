#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "ast/token.hpp"

namespace l::diag {

enum class Severity {
  Error,
  Warning,
};

struct Diagnostic {
  Severity severity = Severity::Error;
  ast::SourceLocation loc;
  std::string message;
};

// Central error/warning sink. Frontend MUST NOT throw/exit on language errors —
// record here and keep recovering so one run can report many diagnostics.
class DiagnosticEngine {
public:
  void error(const ast::SourceLocation& loc, const std::string& message);
  void warning(const ast::SourceLocation& loc, const std::string& message);

  bool hasErrors() const;
  bool hasWarnings() const;
  std::size_t errorCount() const;
  const std::vector<Diagnostic>& all() const { return diagnostics_; }

  // Stable report order: filename, then line, then column.
  std::vector<Diagnostic> sorted() const;

  // Print `[ERROR] file.l:line:col - Message` (and `[WARNING] ...`) lines.
  void report(std::ostream& out) const;

private:
  void add(Severity severity, const ast::SourceLocation& loc, const std::string& message);

  std::vector<Diagnostic> diagnostics_;
};

}  // namespace l::diag
