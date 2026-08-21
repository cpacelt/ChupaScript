// Чем платит UniDyn-контейнер за то, что ребёнок знает свой элемент.
//
// Контейнер порождает по вьюшке на элемент массива. Выражения в пропсах
// ребёнка должны как-то добраться до СВОЕГО элемента, и обсуждаются два
// способа. Мерить их надо не «вообще», а по двум разным критериям — они
// живут в разных бюджетах (см. шапку host_benchmark.cpp):
//
//   компиляция — раз на приход экрана, критерий: задержка первого кадра;
//   вычисление — на каждом кадре, критерий: бюджет кадра.
//
// ─── Способ П (подстановка) ────────────────────────────────────────────
// Прототип написан с дырой: `{item}.count`. Контейнер перед компиляцией
// подставляет туда путь до элемента и компилирует РЕЗУЛЬТАТ — своё
// выражение на каждого ребёнка:
//
//     reactions_7[3].count
//
// Движку не нужно ничего. Выражений столько же, сколько детей.
//
// ─── Способ А (аргумент) ───────────────────────────────────────────────
// Прототип компилируется ОДИН раз с объявленным параметром:
//
//     item.count
//
// и вычисляется по разу на ребёнка, получая свой элемент аргументом.
// Выражение одно на прототип. Движку нужна новая дверь.
//
// Здесь меряется вычисление обеих форм и компиляция обеих форм. Разницу
// в ЧИСЛЕ компиляций даёт не замер, а арифметика: у П их столько же,
// сколько детей, у А — одна на прототип. Блок «экран целиком» эту
// арифметику и считает, чтобы её не пришлось делать в уме.
//
// Чего здесь НЕТ. Доставка аргумента в способе А не измерена: двери
// `chupa_eval_with` ещё не существует, и подделывать её замером
// чего-то другого — врать. Форма `item.count` меряется как есть; цена
// самой передачи — запись указателя в кадр вычисления, и она на два
// порядка меньше всего, что здесь напечатано.
//
// Обстановка нарочно взята из живого экрана, а не круглая: панель реакций
// под постом ленты. Четырнадцать кнопок (столько типов реакций), тридцать
// карточек на экране, шесть выражений на кнопку — иконка, счётчик, флаг
// выбранности, цвет фона, цвет обводки, видимость.
#include <benchmark/benchmark.h>

#include <string>
#include <string_view>
#include <vector>

#include "chupascript/chupascript.h"

namespace {

constexpr int kButtons = 14;  // типов реакций
constexpr int kCards = 30;    // карточек на экране
constexpr int kProps = 6;     // выражений на кнопку

/// Массив реакций одной карточки: ключ и счётчик, как договорились в макете.
std::string reactionsLiteral() {
    std::string out = "[";
    for (int i = 0; i < kButtons; ++i) {
        if (i != 0) { out += ", "; }
        out += "{'key': 'reaction_" + std::to_string(i) +
               "', 'count': " + std::to_string(i * 13) + ", 'mine': false}";
    }
    return out + "]";
}

/// Один элемент того же массива — то, что в способе А приезжает аргументом.
constexpr std::string_view kItem =
    "{'key': 'reaction_3', 'count': 39, 'mine': false}";

bool put(ChupaContext *ctx, std::string_view name, std::string_view text) {
    return chupa_context_set_data(ctx, name.data(), name.size(), text.data(),
                                  text.size());
}

/// Контекст одной карточки: массив реакций, элемент под именем параметра и
/// переменная выбранной реакции — её читают оба способа одинаково.
ChupaContext *furnished() {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr) { return nullptr; }
    static const std::string reactions = reactionsLiteral();
    if (!put(ctx, "reactions", reactions) || !put(ctx, "item", kItem) ||
        !chupa_context_set_string(ctx, "selected", 8, "reaction_3", 10)) {
        chupa_context_destroy(ctx);
        return nullptr;
    }
    return ctx;
}

/// Шесть пропсов кнопки в обеих формах. Выражения одинаковы во всём, кроме
/// того, как названо «своё»: этим и отличаются способы.
std::vector<std::string> props(bool substituted, int index) {
    const std::string self =
        substituted ? "reactions[" + std::to_string(index) + "]" : "item";
    return {
        self + ".key",
        self + ".count",
        self + ".key == selected",
        self + ".count > 0 ? '#F7F4F2' : '#00000000'",
        self + ".key == selected ? '#D76200' : '#83665629'",
        self + ".count > 0",
    };
}

// ══════════════════════════════════════════════════════════════════════════
// Блок 1. Компиляция одного выражения. Раз на экран — задержка.
// ══════════════════════════════════════════════════════════════════════════

void runCompile(benchmark::State &state, bool substituted) {
    ChupaContext *ctx = furnished();
    if (ctx == nullptr) {
        state.SkipWithError("setup failed");
        return;
    }
    const std::vector<std::string> sources = props(substituted, 3);
    state.SetLabel(sources[1] + "  (и ещё 5)");

    for (auto _ : state) {
        for (const std::string &src : sources) {
            ChupaExpression *e =
                chupa_compile_expression(ctx, src.data(), src.size());
            if (e == nullptr) {
                state.SkipWithError("compile failed");
                break;
            }
            chupa_expression_destroy(e);
        }
    }
    state.SetItemsProcessed(state.iterations() * kProps);
    chupa_context_destroy(ctx);
}

void BM_UniDyn_Compile_Substituted(benchmark::State &state) {
    runCompile(state, true);
}
BENCHMARK(BM_UniDyn_Compile_Substituted);

void BM_UniDyn_Compile_Argument(benchmark::State &state) {
    runCompile(state, false);
}
BENCHMARK(BM_UniDyn_Compile_Argument);

// ══════════════════════════════════════════════════════════════════════════
// Блок 2. Вычисление кнопки — шесть пропсов. На каждом кадре — бюджет кадра.
// ══════════════════════════════════════════════════════════════════════════
//
// Это и есть настоящий вопрос к способам: путь `reactions[3].count` длиннее
// пути `item.count` на индексацию массива. Здесь видно, на сколько.

void runEval(benchmark::State &state, bool substituted) {
    ChupaContext *ctx = furnished();
    if (ctx == nullptr) {
        state.SkipWithError("setup failed");
        return;
    }
    const std::vector<std::string> sources = props(substituted, 3);
    std::vector<ChupaExpression *> compiled;
    for (const std::string &src : sources) {
        ChupaExpression *e =
            chupa_compile_expression(ctx, src.data(), src.size());
        if (e == nullptr) {
            state.SkipWithError("compile failed");
            chupa_context_destroy(ctx);
            return;
        }
        compiled.push_back(e);
    }
    state.SetLabel(sources[1] + "  (и ещё 5)");

    for (auto _ : state) {
        for (ChupaExpression *e : compiled) {
            ChupaValue out;
            if (!chupa_eval(ctx, e, &out)) {
                state.SkipWithError("eval failed");
                break;
            }
            benchmark::DoNotOptimize(out);
        }
    }
    state.SetItemsProcessed(state.iterations() * kProps);

    for (ChupaExpression *e : compiled) { chupa_expression_destroy(e); }
    chupa_context_destroy(ctx);
}

void BM_UniDyn_Eval_Substituted(benchmark::State &state) {
    runEval(state, true);
}
BENCHMARK(BM_UniDyn_Eval_Substituted);

void BM_UniDyn_Eval_Argument(benchmark::State &state) { runEval(state, false); }
BENCHMARK(BM_UniDyn_Eval_Argument);

// ══════════════════════════════════════════════════════════════════════════
// Блок 3. Экран целиком: тридцать карточек по четырнадцать кнопок.
// ══════════════════════════════════════════════════════════════════════════
//
// Здесь способы расходятся по-настоящему, и расходятся они в ЧИСЛЕ
// компиляций, а не в цене одной. Прототип разбирается на каждую карточку —
// значит:
//
//   П: kCards * kButtons * kProps  выражений  (своё на каждого ребёнка)
//   А: kCards * kProps             выражений  (одно на прототип карточки)
//
// Разница ровно в kButtons раз. Вычислений на кадр поровну — по одному на
// каждый пропс каждой кнопки, — поэтому кадр меряется один на оба способа
// и различается только ценой пути.

void runScreenCompile(benchmark::State &state, bool substituted) {
    ChupaContext *ctx = furnished();
    if (ctx == nullptr) {
        state.SkipWithError("setup failed");
        return;
    }
    const int perCard = substituted ? kButtons : 1;
    state.SetLabel(std::to_string(kCards * perCard * kProps) + " выражений");

    for (auto _ : state) {
        for (int card = 0; card < kCards; ++card) {
            for (int b = 0; b < perCard; ++b) {
                for (const std::string &src : props(substituted, b)) {
                    ChupaExpression *e =
                        chupa_compile_expression(ctx, src.data(), src.size());
                    if (e == nullptr) {
                        state.SkipWithError("compile failed");
                        break;
                    }
                    chupa_expression_destroy(e);
                }
            }
        }
    }
    chupa_context_destroy(ctx);
}

void BM_UniDyn_Screen_Compile_Substituted(benchmark::State &state) {
    runScreenCompile(state, true);
}
BENCHMARK(BM_UniDyn_Screen_Compile_Substituted)->Unit(benchmark::kMillisecond);

void BM_UniDyn_Screen_Compile_Argument(benchmark::State &state) {
    runScreenCompile(state, false);
}
BENCHMARK(BM_UniDyn_Screen_Compile_Argument)->Unit(benchmark::kMillisecond);

/// Кадр: все кнопки всех карточек, по шесть пропсов на кнопку. Выражения
/// скомпилированы заранее — на кадре компиляции нет ни у одного способа.
void runScreenFrame(benchmark::State &state, bool substituted) {
    ChupaContext *ctx = furnished();
    if (ctx == nullptr) {
        state.SkipWithError("setup failed");
        return;
    }
    std::vector<ChupaExpression *> compiled;
    for (int b = 0; b < kButtons; ++b) {
        for (const std::string &src : props(substituted, b)) {
            ChupaExpression *e =
                chupa_compile_expression(ctx, src.data(), src.size());
            if (e == nullptr) {
                state.SkipWithError("compile failed");
                chupa_context_destroy(ctx);
                return;
            }
            compiled.push_back(e);
        }
    }
    state.SetLabel(std::to_string(kCards * kButtons * kProps) + " вычислений");

    for (auto _ : state) {
        for (int card = 0; card < kCards; ++card) {
            for (ChupaExpression *e : compiled) {
                ChupaValue out;
                if (!chupa_eval(ctx, e, &out)) {
                    state.SkipWithError("eval failed");
                    break;
                }
                benchmark::DoNotOptimize(out);
            }
        }
    }

    for (ChupaExpression *e : compiled) { chupa_expression_destroy(e); }
    chupa_context_destroy(ctx);
}

void BM_UniDyn_Screen_Frame_Substituted(benchmark::State &state) {
    runScreenFrame(state, true);
}
BENCHMARK(BM_UniDyn_Screen_Frame_Substituted)->Unit(benchmark::kMillisecond);

void BM_UniDyn_Screen_Frame_Argument(benchmark::State &state) {
    runScreenFrame(state, false);
}
BENCHMARK(BM_UniDyn_Screen_Frame_Argument)->Unit(benchmark::kMillisecond);

}  // namespace
