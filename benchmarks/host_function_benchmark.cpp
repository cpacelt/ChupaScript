// LAYOUT — five benchmarks, one register call each, same eval-and-deliver
// shape as benchmarks/host_benchmark.cpp's block 3: compile once, outside the
// loop; evaluate and consume the result inside it, because that is what a
// host actually does with an expression prop. They answer THREE separate
// questions, and each pair below is labelled with the one it answers — do
// not average across pairs, they are not measuring the same thing.
//
// Q1. WHAT DOES DISPATCH THROUGH A POINTER COST, ISOLATED FROM WORK?
//   BM_Host_Builtin_Abs   abs(x) — one numeric argument, one global lookup,
//                         one trivial fabs. Dispatched by a compile-time
//                         index into the builtin table.
//   BM_Host_CallNumber    hostAbs(x) — the same shape exactly: one numeric
//                         argument, the same global, the same fabs, done
//                         inside the callback instead. The only thing that
//                         differs between this pair is the dispatch
//                         mechanism (index vs function pointer) plus the C
//                         API boundary — argument count, argument kind and
//                         the amount of work are held equal, so the gap is
//                         the pointer-call price and nothing else.
//
//   Rejected basis: count(items). It also isolates a host-vs-builtin gap,
//   but count takes an argument hostConst() (its old opposite number) did
//   not, and walks a whole array besides — two effects of opposite sign
//   (dispatch mechanism, and extra work) land in one number, which answers
//   neither question honestly. abs/hostAbs is the pair that isolates only
//   dispatch: same arity, same argument kind, same triviality of the work.
//
// Q2. WHAT DOES A HOST FUNCTION COST AGAINST A TYPICAL BUILTIN IN USE?
//   BM_Host_Builtin_Count count(items) — a builtin doing real, typical work:
//                         reads a global, walks an aggregate.
//   BM_Host_CallVoid      hostConst() — a host function with nothing to do
//                         and nothing to marshal. NOT a fair dispatch-only
//                         pair with Count (see Q1's rejected basis) — this
//                         pair answers a different, still useful question:
//                         when a prop is a typical builtin call today, is a
//                         host function that replaced it with a no-op look
//                         cheaper or costlier in absolute terms? It is not
//                         "the price of the pointer" and is not labelled as
//                         such below or in the docs.
//
// Q3. WHAT DOES ONE STRING ARGUMENT ADD, ON TOP OF THE HOST MECHANISM?
//   BM_Host_CallString    hostLen(s) — the same host mechanism as
//                         BM_Host_CallVoid, one string argument added. The
//                         gap to CallVoid isolates the cost of handing a
//                         BOXED argument across the boundary — a number
//                         rides in the ChupaValue's own bytes, a string does
//                         not, and chupa_value_string has to read it out.
//
// Both host functions used in Q1/Q3 (hostAbs, hostLen) and hostConst are
// CHUPA_FN_PURE: docs/semantics.md §3.2 forbids calling anything else inside
// an Expression tree, and an expression — not a script statement — is what
// every other block in host_benchmark.cpp measures, so this file keeps the
// same mode for the same reason.
#include <benchmark/benchmark.h>

#include <cmath>
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

/// hostAbs(x): one numeric argument, fabs of it out — same shape as abs(x),
/// so the gap to abs(x) is dispatch alone (see Q1 above).
bool hostAbsCall(ChupaContext * /*ctx*/, const ChupaValue *args,
                 std::size_t /*argc*/, ChupaValue *out, void * /*userData*/) {
    const double n = chupa_value_number(&args[0]);
    chupa_make_number(out, std::fabs(n));
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

// ─── Q1: dispatch price, isolated — abs(x) vs hostAbs(x) ───

void BM_Host_Builtin_Abs(benchmark::State &state) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr || !chupa_context_set_number(ctx, "x", 1, -5.5)) {
        state.SkipWithError("furnish failed");
        chupa_context_destroy(ctx);   // no-op on nullptr; a leak otherwise
        return;
    }
    runEvalNumber(state, ctx, "abs(x)");
}
BENCHMARK(BM_Host_Builtin_Abs);

void BM_Host_CallNumber(benchmark::State &state) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr || !chupa_context_set_number(ctx, "x", 1, -5.5)) {
        state.SkipWithError("furnish failed");
        chupa_context_destroy(ctx);   // no-op on nullptr; a leak otherwise
        return;
    }
    ChupaFunction fn{};
    fn.name = "hostAbs";
    fn.name_len = 7;
    fn.min_args = 1;
    fn.max_args = 1;
    fn.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_PURE;
    fn.call = hostAbsCall;
    if (!chupa_register(ctx, &fn)) {
        state.SkipWithError(errorMessage(ctx));
        chupa_context_destroy(ctx);
        return;
    }
    runEvalNumber(state, ctx, "hostAbs(x)");
}
BENCHMARK(BM_Host_CallNumber);

// ─── Q2: host function vs a typical builtin doing real work ───

void BM_Host_Builtin_Count(benchmark::State &state) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr ||
        !chupa_context_set_data(ctx, "items", 5, "[1, 2, 3, 4, 5]", 15)) {
        state.SkipWithError("furnish failed");
        chupa_context_destroy(ctx);   // no-op on nullptr; a leak otherwise
        return;
    }
    runEvalNumber(state, ctx, "count(items)");
}
BENCHMARK(BM_Host_Builtin_Count);

void BM_Host_CallVoid(benchmark::State &state) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr) {
        state.SkipWithError("chupa_context_create failed");
        return;   // nothing was created, so there is nothing to destroy
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

// ─── Q3: what one string argument adds on top of the host mechanism ───

void BM_Host_CallString(benchmark::State &state) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr) {
        state.SkipWithError("chupa_context_create failed");
        return;   // nothing was created, so there is nothing to destroy
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
