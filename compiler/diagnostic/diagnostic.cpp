#include "diagnostic/diagnostic.hpp"

#include <algorithm>

namespace l::diag {

void DiagnosticEngine::add(Severity severity, const ast::SourceLocation& loc,
                           const std::string& message) {
  Diagnostic d;
  d.severity = severity;
  d.loc = loc;
  d.message = message;
  diagnostics_.push_back(std::move(d));
}

void DiagnosticEngine::error(const ast::SourceLocation& loc, const std::string& message) {
  add(Severity::Error, loc, message);
}

void DiagnosticEngine::warning(const ast::SourceLocation& loc, const std::string& message) {
  add(Severity::Warning, loc, message);
}

bool DiagnosticEngine::hasErrors() const {
  for (const Diagnostic& d : diagnostics_) {
    if (d.severity == Severity::Error) {
      return true;
    }
  }
  return false;
}

bool DiagnosticEngine::hasWarnings() const {
  for (const Diagnostic& d : diagnostics_) {
    if (d.severity == Severity::Warning) {
      return true;
    }
  }
  return false;
}

std::size_t DiagnosticEngine::errorCount() const {
  std::size_t n = 0;
  for (const Diagnostic& d : diagnostics_) {
    if (d.severity == Severity::Error) {
      ++n;
    }
  }
  return n;
}

std::vector<Diagnostic> DiagnosticEngine::sorted() const {
  std::vector<Diagnostic> out = diagnostics_;
  std::stable_sort(out.begin(), out.end(), [](const Diagnostic& a, const Diagnostic& b) {
    if (a.loc.filename != b.loc.filename) {
      return a.loc.filename < b.loc.filename;
    }
    if (a.loc.line != b.loc.line) {
      return a.loc.line < b.loc.line;
    }
    return a.loc.column < b.loc.column;
  });
  return out;
}

void DiagnosticEngine::report(std::ostream& out) const {
  // Strict format: [ERROR] file.l:line:col - Message
  for (const Diagnostic& d : sorted()) {
    const char* tag = (d.severity == Severity::Warning) ? "[WARNING]" : "[ERROR]";
    const std::string& file = d.loc.filename.empty() ? "<unknown>" : d.loc.filename;
    out << tag << ' ' << file << ':' << d.loc.line << ':' << d.loc.column << " - " << d.message
        << '\n';
  }
}

}  // namespace l::diag
