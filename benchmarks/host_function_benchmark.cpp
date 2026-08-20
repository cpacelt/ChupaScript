// LAYOUT — three benchmarks, one register call each, same eval-and-deliver
// shape as benchmarks/host_benchmark.cpp's block 3: compile once, outside the
// loop; evaluate and consume the result inside it, because that is what a
// host actually does with an expression prop.
//
// What is measured and why THIS shape:
//
//   BM_Host_Builtin_Count  count(items) — the cheapest arithmetic builtin,
//                          dispatched by a compile-time index, no pointer
//                          indirection and no host-side argument marshalling.
//   BM_Host_CallVoid       a host function taking NO arguments, returning a
//                          number through the C ABI. The gap to Count is the
//                          price of going through a function pointer at all:
//                          the indirect call itself plus whatever the C API
//                          boundary costs on a call with nothing to marshal.
//   BM_Host_CallString     the same host mechanism, one string argument
//                          added. The gap to CallVoid isolates the cost of
//                          handing a BOXED argument across that boundary —
//                          a number rides in the ChupaValue's own bytes, a
//                          string does not.
//
// Rejected pairing for BM_Host_Builtin_Count: no builtin in this language
// takes zero arguments (core/src/builtin.cpp — every entry has minArgs >= 1),
// so there is no zero-argument builtin to pair CallVoid against honestly.
// count(items) is the next best thing — the cheapest builtin that returns a
// number — and it is asked to translate one argument that CallVoid does not
// have, which understates the pointer-call price rather than flattering it.
//
// Both host functions are CHUPA_FN_PURE: docs/semantics.md §3.2 forbids
// calling anything else inside an Expression tree, and an expression — not a
// script statement — is what every other block in host_benchmark.cpp
// measures, so this file keeps the same mode for the same reason.
#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "chupascript/chupascript.h"

namespace {

/// Same shape as host_benchmark.cpp's errorMessage — off the SkipWithError
/// path only, not part of what any block below measures.
std::string errorMessage(ChupaContext *ctx) {
    ChupaError err;
    chupa_context_error(ctx, &err);
    return std::string(err.message, err.message_len);
}

/// hostConst(): zero arguments, one constant number out — the whole call
/// carries nothing to marshal in either direction.
bool hostConstCall(ChupaContext * /*ctx*/, const ChupaValue * /*args*/,
                   std::size_t /*argc*/, ChupaValue *out,
                   void * /*userData*/) {
    chupa_make_number(out, 42.0);
    return true;
}

/// hostLen(s): one string argument, its length out — the smallest call that
/// still has to unbox an argument.
bool hostLenCall(ChupaContext * /*ctx*/, const ChupaValue *args,
                 std::size_t /*argc*/, ChupaValue *out, void * /*userData*/) {
    const char *bytes = nullptr;
    std::size_t len = 0;
    chupa_value_string(&args[0], &bytes, &len);
    chupa_make_number(out, static_cast<double>(len));
    return true;
}

/// Compiles source once, then evaluates it in the timed loop, delivering the
/// number result the same way a host reading a numeric prop would.
void runEvalNumber(benchmark::State &state, ChupaContext *ctx,
                   std::string_view source) {
    ChupaExpression *e =
        chupa_compile_expression(ctx, source.data(), source.size());
    if (e == nullptr) {
        state.SkipWithError(errorMessage(ctx));
        chupa_context_destroy(ctx);
        return;
    }
    state.SetLabel(std::string(source));
    for (auto _ : state) {
        ChupaValue out{};
        if (!chupa_eval(ctx, e, &out)) {
            state.SkipWithError(errorMessage(ctx));
            break;
        }
        double delivered = chupa_value_number(&out);
        benchmark::DoNotOptimize(delivered);
    }
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

void BM_Host_Builtin_Count(benchmark::State &state) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr ||
        !chupa_context_set_data(ctx, "items", 5, "[1, 2, 3, 4, 5]", 15)) {
        state.SkipWithError("furnish failed");
        return;
    }
    runEvalNumber(state, ctx, "count(items)");
}
BENCHMARK(BM_Host_Builtin_Count);

void BM_Host_CallVoid(benchmark::State &state) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr) {
        state.SkipWithError("chupa_context_create failed");
        return;
    }
    ChupaFunction fn{};
    fn.name = "hostConst";
    fn.name_len = 9;
    fn.min_args = 0;
    fn.max_args = 0;
    fn.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_PURE;
    fn.call = hostConstCall;
    if (!chupa_register(ctx, &fn)) {
        state.SkipWithError(errorMessage(ctx));
        chupa_context_destroy(ctx);
        return;
    }
    runEvalNumber(state, ctx, "hostConst()");
}
BENCHMARK(BM_Host_CallVoid);

void BM_Host_CallString(benchmark::State &state) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr) {
        state.SkipWithError("chupa_context_create failed");
        return;
    }
    ChupaFunction fn{};
    fn.name = "hostLen";
    fn.name_len = 7;
    fn.min_args = 1;
    fn.max_args = 1;
    fn.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_PURE;
    fn.call = hostLenCall;
    if (!chupa_register(ctx, &fn)) {
        state.SkipWithError(errorMessage(ctx));
        chupa_context_destroy(ctx);
        return;
    }
    runEvalNumber(state, ctx, "hostLen('Заголовок дальше некуда')");
}
BENCHMARK(BM_Host_CallString);

}  // namespace
