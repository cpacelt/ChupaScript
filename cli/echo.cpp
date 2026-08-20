#include "echo.hpp"

#include <cassert>
#include <cstddef>

#include "chupascript/chupascript.h"

namespace chupa {
namespace {

/// The callback the engine calls for `echo(x)`.
///
/// ctx is nullptr here: the shell registers through CS::Context::
/// registerFunction directly (see registerEcho below), which — unlike
/// chupa_context_create — never sets the opaque C-API pointer. Reading
/// args[0] needs no context at all, so nullptr is harmless; a callback that
/// tried to use ctx would not be.
bool echoCallback(ChupaContext * /*ctx*/, const ChupaValue *args,
                  std::size_t /*argc*/, ChupaValue * /*out*/,
                  void *userData) {
    const char *bytes = nullptr;
    std::size_t len = 0;
    chupa_value_string(&args[0], &bytes, &len);
    auto *stream = static_cast<std::ostream *>(userData);
    stream->write(bytes, static_cast<std::streamsize>(len));
    *stream << "\n";
    return true;
}

}  // namespace

void registerEcho(CS::Context &ctx, std::ostream &out) {
    ChupaFunction desc{};
    desc.name = "echo";
    desc.name_len = 4;
    desc.min_args = 1;
    desc.max_args = 1;
    desc.flags = CHUPA_FN_NONE;  // no RETURNS_VALUE -> Void; no PURE -> dirty
    desc.call = echoCallback;
    desc.user_data = &out;
    desc.release = nullptr;      // out is borrowed, nothing to release

    // Runs once on a freshly built ctx, before any compilation on it — the
    // one case registerFunction is documented to refuse (TooLate) cannot
    // happen here, so the outcome is only worth an assert, not a branch.
    [[maybe_unused]] const CS::RegisterOutcome outcome =
        ctx.registerFunction(desc);
    assert(outcome == CS::RegisterOutcome::Ok);
}

}  // namespace chupa
