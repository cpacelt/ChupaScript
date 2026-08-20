// A program that deliberately leaks one cycle, so that the leak detector has
// something to find.
//
// Not a gtest case: a passing test suite must stay green, and this program's
// whole purpose is to make the sanitizer report a leak. There is no add_test
// for it, so an ordinary build never runs it. tools/asan.sh runs it
// separately and requires the LeakSanitizer marker in its captured output — a
// non-zero exit alone would not tell a real leak from cycle_leak_main.cpp's
// own setup failures.
//
// Reference counting will never collect this. The language allows it in two
// lines (docs/semantics.md §2.3), a collector would cost more than the rest of
// the engine, and the BDUI screens this engine serves receive their data as a
// tree from the backend. So the limitation is documented, and the tooling
// makes an accidental one visible instead of silent.
#include <cstdio>

#include "context.hpp"

int main() {
    CS::Context ctx;
    CS::Diagnostic diag;
    if (!ctx.setVariableText("state", "{'items': []}", diag)) {
        std::fputs("setup failed\n", stderr);
        return 2;
    }

    CS::Script script;
    CS::Diagnostic diags[1];
    if (ctx.compileScript("state['self'] = state;", &script, diags, 1) != 0) {
        std::fputs(diags[0].message, stderr);
        return 2;
    }
    if (!ctx.run(script, diag)) {
        std::fputs(diag.message, stderr);
        return 2;
    }
    // The Context is destroyed here; the object holding itself is not.
    return 0;
}
