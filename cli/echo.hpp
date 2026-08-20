#pragma once
#include <ostream>

#include "context.hpp"

namespace chupa {

/// Registers `echo(x)` on ctx: void, one argument, prints it to `out`.
///
/// This is the shell's one demonstration host function — the shell is the
/// only live host in the repository, so without it the registration
/// mechanism (docs/superpowers/sdd/2026-08-20-host-functions) has nowhere to
/// be touched by hand. There is no `:register` command and no way to
/// register a function from a script, so this call is the only source of a
/// host function the shell ever has.
///
/// Declared WITHOUT CHUPA_FN_PURE: printing is an effect visible outside the
/// engine, and marking it pure would license the engine to skip or reorder
/// the call. That also makes it callable only as a script statement, never
/// inside an expression — `echo('привет');`, not `expr: echo('привет')`.
///
/// `out` is borrowed and must outlive every call made through `ctx` — the
/// shell passes std::cout, a test passes an std::ostringstream to read back
/// what was printed.
///
/// Must run before the first compilation on `ctx` (registerFunction's own
/// rule) — the shell does it once per fresh context, right after
/// construction, not per line.
void registerEcho(CS::Context &ctx, std::ostream &out);

}  // namespace chupa
