// Бенчмарки парсера, по одному на подмножество грамматики.
//
// Замеряется разбор целиком: поток токенов лексера плюс построение дерева.
// Отдельно лексер уже замерен в lexer_benchmark.cpp.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "parser.hpp"

namespace {

/// "unit sep unit sep … unit"
std::string join(const std::string &unit, const std::string &separator,
                 int times) {
    std::string result;
    result.reserve((unit.size() + separator.size()) *
                   static_cast<std::size_t>(times));
    for (int i = 0; i < times; ++i) {
        if (i != 0) {
            result += separator;
        }
        result += unit;
    }
    return result;
}

std::string repeat(const std::string &unit, int times) {
    return join(unit, "", times);
}

void runExpression(benchmark::State &state, const std::string &source) {
    for (auto _ : state) {
        CS::Ast ast;
        CS::Diagnostic diag;
        bool ok = CS::parseExpression(
            source.data(), static_cast<std::uint32_t>(source.size()), ast, diag);
        if (!ok) {
            state.SkipWithError("parse failed");
            return;
        }
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(ast);
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                            static_cast<std::int64_t>(source.size()));
}

void runScript(benchmark::State &state, const std::string &source) {
    for (auto _ : state) {
        CS::Ast ast;
        CS::Diagnostic diag;
        bool ok = CS::parseScript(
            source.data(), static_cast<std::uint32_t>(source.size()), ast, diag);
        if (!ok) {
            state.SkipWithError("parse failed");
            return;
        }
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(ast);
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                            static_cast<std::int64_t>(source.size()));
}

}  // namespace

// Цена спуска: на каждый операнд проходится вся цепочка из десяти правил.
static void BM_Parse_Chain(benchmark::State &state) {
    runExpression(state, join("a", " + ", 400));
}
BENCHMARK(BM_Parse_Chain);

static void BM_Parse_Precedence(benchmark::State &state) {
    runExpression(state, join("a * b + c < d", " || ", 200));
}
BENCHMARK(BM_Parse_Precedence);

static void BM_Parse_Postfix(benchmark::State &state) {
    runExpression(state, join("user.profile.name[0].value", " + ", 150));
}
BENCHMARK(BM_Parse_Postfix);

static void BM_Parse_Aggregates(benchmark::State &state) {
    runExpression(state, join("{ 'a': 1, 'b': [1, 2, 3] }", " + ", 150));
}
BENCHMARK(BM_Parse_Aggregates);

static void BM_Parse_Props(benchmark::State &state) {
    runExpression(state,
                  join("(product.discount != null"
                       " ? product.price * (1 - product.discount)"
                       " : product.price)",
                       " + ", 100));
}
BENCHMARK(BM_Parse_Props);

static void BM_Parse_Handler(benchmark::State &state) {
    runScript(state, repeat("push(state.items, product);"
                             "state.badge = count(state.items);"
                             "state.total += product.price;"
                             "state.label = format('добавлено ${} на ${}',"
                             " product.name, product.price);",
                             50));
}
BENCHMARK(BM_Parse_Handler);
