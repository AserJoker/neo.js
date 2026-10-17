#include "neojs/frontend/diagnostics.h"

namespace neojs {

void DiagnosticSink::print(FILE *out) const {
  for (const Diagnostic &d : diagnostics_) {
    const char *sev = "info";
    switch (d.severity) {
      case Severity::Error:
        sev = "error";
        break;
      case Severity::Warning:
        sev = "warning";
        break;
      case Severity::Info:
        sev = "info";
        break;
    }
    std::fprintf(out, "%zu:%zu: %s: [%s] %s\n", d.loc.line, d.loc.col, sev,
                 d.code.c_str(), d.message.c_str());
  }
}

}  // namespace neojs
