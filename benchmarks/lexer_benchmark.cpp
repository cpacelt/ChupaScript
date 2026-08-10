// Бенчмарки лексера, по одному на подмножество лексики.
//
// Каждый становится осмысленным, когда садится его порция реализации, и с
// этого момента обязан не деградировать от последующих порций.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

#include "diagnostic.hpp"
#include "lexer.hpp"
#include "token.hpp"

namespace {

void lexAll(const std::string &source) {
    CS::Lexer lexer(source.data(), static_cast<std::uint32_t>(source.size()));
    CS::Token token;
    CS::Diagnostic diag;
    while (lexer.next(token, diag) && token.kind != CS::TokenKind::End) {
        benchmark::DoNotOptimize(token);
    }
}

std::string repeat(const std::string &unit, int times) {
    std::string result;
    result.reserve(unit.size() * static_cast<std::size_t>(times));
    for (int i = 0; i < times; ++i) {
        result += unit;
    }
    return result;
}

void run(benchmark::State &state, const std::string &source) {
    for (auto _ : state) {
        lexAll(source);
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                            static_cast<std::int64_t>(source.size()));
}

}  // namespace

static void BM_Lex_Trivia(benchmark::State &state) {
    run(state, repeat("   /* комментарий */\n// строка\n", 200));
}
BENCHMARK(BM_Lex_Trivia);

static void BM_Lex_Punctuators(benchmark::State &state) {
    run(state, repeat("+= -= == != <= >= && || ?? ( ) [ ] { } , : ; . ? ", 200));
}
BENCHMARK(BM_Lex_Punctuators);

static void BM_Lex_Identifiers(benchmark::State &state) {
    run(state, repeat("user state items product price count true false null ", 200));
}
BENCHMARK(BM_Lex_Identifiers);

static void BM_Lex_Numbers(benchmark::State &state) {
    run(state, repeat("0 1 42 3.0 0.5 1000000 12.75 ", 200));
}
BENCHMARK(BM_Lex_Numbers);

static void BM_Lex_Strings(benchmark::State &state) {
    run(state, repeat("'простая' 'с \\n экранированием' 'юникод 😀' ", 200));
}
BENCHMARK(BM_Lex_Strings);

static void BM_Lex_Realistic(benchmark::State &state) {
    run(state, repeat(
                   "push(state.items, product);"
                   "state.badge = count(state.items);"
                   "state.total += product.price;"
                   "state.label = format('добавлено ${} на ${}',"
                   " product.name, product.price);",
                   50));
}
BENCHMARK(BM_Lex_Realistic);
