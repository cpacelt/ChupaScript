// Замеры по жизненному циклу хоста, а не по внутренним примитивам.
//
// Прочие файлы в этой папке мерят составляющие: одну аллокацию, один поиск,
// один обход. Это нужно, но по ним нельзя решить, стала ли система быстрее:
// естественно, что обращение к аллокатору дороже сдвига указателя в арене, и
// узнать это можно было не измеряя. Решает, сколько это стоит там, где работа
// происходит на самом деле.
//
// А происходит она так. С бэкенда приходит JSON. В нём есть глобальные
// переменные экрана — хост кладёт их в контекст один раз. Дальше JSON
// описывает иерархию вьюшек, и у каждого пропса значением может быть
// выражение: оно компилируется один раз и вычисляется на каждом кадре.
// Обработчики действий — скрипты, тоже компилируются один раз и выполняются
// по нажатию.
//
// Отсюда четыре блока, и у них РАЗНЫЕ критерии. Путать их нельзя.
//
//   Блок 1, простановка переменных — раз на приход данных. Задержка.
//   Блок 2, компиляция выражений   — раз на экран. Задержка первого кадра.
//   Блок 3, вычисление и выдача    — на каждом кадре. Бюджет кадра.
//   Блок 4, скрипты                — компиляция раз, выполнение по нажатию.
//                                    Обе половины — задержка.
//   Блок 5, кадр целиком           — сводка, по которой и принимается решение.
//
// Просадка компиляции на треть почти ничего не стоит; просадка вычисления на
// пять процентов стоит много. Сравнивать их одной меркой — ошибка.
//
// Всё идёт через C API, а не через C++ напрямую: хост ходит только так, и
// граница — часть измеряемого. У выражения меряется не «вычисление», а
// вычисление ВМЕСТЕ с доставкой результата вызывающему: скаляр копией, строка
// срезом и копией в буфер хоста (обёртка делает ровно это), агрегат обходом.
// Без доставки замер отвечал бы не на тот вопрос.
#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "chupascript/chupascript.h"

namespace {

/// Same string chupa_context_error(ctx, &err).message used to hand back
/// directly, off the SkipWithError path only — this is not part of what
/// any block measures.
std::string errorMessage(ChupaContext *ctx) {
    ChupaError err;
    chupa_context_error(ctx, &err);
    return std::string(err.message, err.message_len);
}

// ─── общая обстановка: данные одного экрана ───

constexpr std::string_view kUser =
    "{'first': 'Вася', 'last': 'Пупкин', 'age': 30, 'city': 'Москва'}";
constexpr std::string_view kFlags = "{'active': true, 'hidden': false}";

/// Список из двадцати карточек — то, из чего состоит лента.
std::string cardList(int count) {
    std::string out = "[";
    for (int i = 0; i < count; ++i) {
        if (i != 0) { out += ", "; }
        out += "{'id': " + std::to_string(i) + ", 'label': 'Карточка " +
               std::to_string(i) + "', 'price': " + std::to_string(i * 17) + "}";
    }
    out += "]";
    return out;
}

bool put(ChupaContext *ctx, std::string_view name, std::string_view text) {
    return chupa_context_set_data(ctx, name.data(), name.size(), text.data(),
                                  text.size());
}

/// Обстановка, на которой меряются блоки 2–4.
bool furnish(ChupaContext *ctx) {
    static const std::string cards = cardList(20);
    return chupa_context_set_number(ctx, "width", 5, 320.0) &&
           chupa_context_set_string(ctx, "title", 5, "Заголовок", 18) &&
           put(ctx, "user", kUser) && put(ctx, "flags", kFlags) &&
           put(ctx, "items", cards);
}

// ══════════════════════════════════════════════════════════════════════════
// Блок 1. Простановка переменных. Раз на приход данных — критерий задержка.
// ══════════════════════════════════════════════════════════════════════════
//
// Оси две: вид значения и его размер. Размер здесь важнее вида: стоимость
// узлов растёт по числу элементов, а не по тому, массив это или объект.
// Контекст создаётся вне цикла, имя одно и то же — значит меряется перезапись
// глобальной переменной, а она и происходит на каждом обновлении данных.

void runSet(benchmark::State &state, std::string_view text) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr) {
        state.SkipWithError("chupa_context_create failed");
        return;
    }
    state.SetLabel(std::string(text.substr(0, 60)));
    for (auto _ : state) {
        if (!put(ctx, "data", text)) {
            state.SkipWithError("chupa_context_set_data failed");
            break;
        }
    }
    chupa_context_destroy(ctx);
}

void BM_Host_Set_Bool(benchmark::State &state) {
    ChupaContext *ctx = chupa_context_create();
    state.SetLabel("true");
    for (auto _ : state) {
        if (!chupa_context_set_bool(ctx, "data", 4, true)) {
            state.SkipWithError("set_bool failed");
            break;
        }
    }
    chupa_context_destroy(ctx);
}
BENCHMARK(BM_Host_Set_Bool);

void BM_Host_Set_Number(benchmark::State &state) {
    ChupaContext *ctx = chupa_context_create();
    state.SetLabel("320");
    for (auto _ : state) {
        if (!chupa_context_set_number(ctx, "data", 4, 320.0)) {
            state.SkipWithError("set_number failed");
            break;
        }
    }
    chupa_context_destroy(ctx);
}
BENCHMARK(BM_Host_Set_Number);

/// Короткая и длинная строка порознь: у короткой всю цену составляет
/// аллокация, у длинной — копия байт. Одной строкой их не различить.
void BM_Host_Set_String(benchmark::State &state, std::string_view text) {
    ChupaContext *ctx = chupa_context_create();
    state.SetLabel("строка " + std::to_string(text.size()) + " байт");
    for (auto _ : state) {
        if (!chupa_context_set_string(ctx, "data", 4, text.data(),
                                      text.size())) {
            state.SkipWithError("set_string failed");
            break;
        }
    }
    chupa_context_destroy(ctx);
}
BENCHMARK_CAPTURE(BM_Host_Set_String, Short, std::string_view("Вася"));
BENCHMARK_CAPTURE(BM_Host_Set_String, Long,
                  std::string_view("Длинное название карточки, какое приходит "
                                   "с бэкенда в ленте товаров"));

void BM_Host_Set_Array10(benchmark::State &state) {
    runSet(state, "[0,1,2,3,4,5,6,7,8,9]");
}
BENCHMARK(BM_Host_Set_Array10);

void BM_Host_Set_Array1000(benchmark::State &state) {
    static const std::string text = [] {
        std::string s = "[";
        for (int i = 0; i < 1000; ++i) {
            if (i != 0) { s += ','; }
            s += std::to_string(i);
        }
        return s + "]";
    }();
    runSet(state, text);
}
BENCHMARK(BM_Host_Set_Array1000);

void BM_Host_Set_Object3(benchmark::State &state) {
    runSet(state, "{'id': 1, 'name': 'Вася', 'active': true}");
}
BENCHMARK(BM_Host_Set_Object3);

void BM_Host_Set_Object20(benchmark::State &state) {
    static const std::string text = [] {
        std::string s = "{";
        for (int i = 0; i < 20; ++i) {
            if (i != 0) { s += ','; }
            s += "'k" + std::to_string(i) + "': " + std::to_string(i);
        }
        return s + "}";
    }();
    runSet(state, text);
}
BENCHMARK(BM_Host_Set_Object20);

void BM_Host_Set_Nested4(benchmark::State &state) {
    runSet(state, "{'a': {'b': {'c': {'d': [1, 2, 3]}}}}");
}
BENCHMARK(BM_Host_Set_Nested4);

/// Лента из двадцати карточек — то, что приходит на самом деле.
void BM_Host_Set_CardList(benchmark::State &state) {
    static const std::string text = cardList(20);
    runSet(state, text);
}
BENCHMARK(BM_Host_Set_CardList);

// ══════════════════════════════════════════════════════════════════════════
// Блоки 2 и 3. Выражения: компиляция и вычисление с выдачей.
// ══════════════════════════════════════════════════════════════════════════
//
// Набор исходников ОДИН на оба блока — в этом вся его польза. Таблица тогда
// читается одной строкой на форму: столько стоит скомпилировать её раз на
// экран, столько — выполнять каждый кадр.
//
// Формы подобраны по двум осям сразу: что возвращается (скаляр, строка,
// массив, объект) и откуда оно взялось (константа в тексте, значение
// переменной, результат вычисления). Ниже эти оси размечены в именах.

struct Form {
    const char *name;
    std::string_view source;
};

constexpr Form kForms[] = {
    // скаляр: константой, из переменной, вычислением
    {"num_const",     "42"},
    {"num_var",       "width"},
    {"num_computed",  "width * 2 + 1"},
    {"bool_computed", "width > 100 && flags.active"},

    // строка: литералом, из переменной, из поля, вычислением
    {"str_const",     "'привет'"},
    {"str_var",       "title"},
    {"str_field",     "user.first"},
    {"str_format1",   "format('${}', width)"},
    {"str_format4",   "format('${} ${} ${} ${}', user.first, user.last, "
                      "user.age, user.city)"},

    // чтение вглубь — то, из чего состоит почти всякий пропс
    {"str_indexed",   "items[3].label"},
    {"num_indexed",   "items[3].price"},
    {"coalesce",      "user.nickname ?? 'Гость'"},

    // массив: литералом, из переменной, вычислением
    {"arr_const",     "[1, 2, 3, 4, 5]"},
    {"arr_var",       "items"},
    {"arr_computed",  "keys(user)"},

    // объект: литералом, из переменной, полем
    {"obj_const",     "{'id': 1, 'name': 'Вася', 'active': true}"},
    {"obj_var",       "user"},

    // билтин, ничего не выделяющий
    {"builtin_count", "count(items)"},

    // составные: так пропсы и выглядят
    {"mix_format",    "format('${} — ${}', items[2].label, items[2].price)"},
    {"mix_ternary",   "width > 200 ? items[1].label : 'узко'"},
};

// ─── блок 2: компиляция ───

void runCompile(benchmark::State &state, std::string_view source) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr || !furnish(ctx)) {
        state.SkipWithError("furnish failed");
        return;
    }
    state.SetLabel(std::string(source));
    for (auto _ : state) {
        ChupaExpression *e =
            chupa_compile_expression(ctx, source.data(), source.size());
        if (e == nullptr) {
            state.SkipWithError(errorMessage(ctx));
            break;
        }
        chupa_expression_destroy(e);
    }
    chupa_context_destroy(ctx);
}

// ─── блок 3: вычисление вместе с выдачей ───
//
// Доставка — часть измеряемого, и для каждого вида она своя, потому что у
// хоста она своя. Скаляр приходит копией. Строка приходит срезом, который
// обёртка немедленно копирует к себе, — значит копия входит в замер, иначе
// строковые формы выглядели бы дешевле, чем стоят. Агрегат обходится целиком:
// хост, получивший массив, читает его, а не любуется дескриптором.
//
// Обход рекурсивный и трогает каждый лист: именно так обёртка строит из
// результата свою структуру.

/// Буфер хоста, куда копируются строки. Один на замер, ёмкость сохраняется —
/// обёртка тоже не выделяет заново на каждый кадр.
std::string g_hostBuffer;

// Обход написан один раз на обе ветки сравнения, и это принципиально: сравнить
// модели можно, только если исходник замера у них общий.
//
// Расходятся они в одном — старой модели для каждого шага нужен контекст:
// значение там индекс в пулы конкретного хранилища, и без него не значит
// ничего. Новой не нужен нигде. Это и есть измеряемая разница, поэтому она
// вынесена в макрос, а не в две копии функции.
#ifdef CHUPA_VALUE_NEEDS_CONTEXT
#define CHUPA_W ctx,
#else
#define CHUPA_W
#endif

void consume(ChupaContext *ctx, ChupaValue v);

void consumeString(ChupaContext *ctx, ChupaValue v) {
    const char *bytes = nullptr;
    size_t len = 0;
    chupa_value_string(CHUPA_W &v, &bytes, &len);
    g_hostBuffer.assign(bytes, len);   // копия к хосту — та самая доставка
    benchmark::DoNotOptimize(g_hostBuffer.data());
    (void)ctx;
}

void consume(ChupaContext *ctx, ChupaValue v) {
    switch (chupa_value_kind(CHUPA_W &v)) {
        case CHUPA_KIND_NULL:
            return;
        case CHUPA_KIND_BOOL: {
            bool b = chupa_value_bool(CHUPA_W &v);
            benchmark::DoNotOptimize(b);
            return;
        }
        case CHUPA_KIND_NUMBER: {
            double d = chupa_value_number(CHUPA_W &v);
            benchmark::DoNotOptimize(d);
            return;
        }
        case CHUPA_KIND_STRING:
            consumeString(ctx, v);
            return;
        case CHUPA_KIND_ARRAY: {
            const size_t n = chupa_array_count(CHUPA_W &v);
            for (size_t i = 0; i < n; ++i) {
                ChupaValue item{};
                chupa_array_at(CHUPA_W &v, i, &item);
                consume(ctx, item);
            }
            return;
        }
        case CHUPA_KIND_OBJECT: {
            const size_t n = chupa_object_count(CHUPA_W &v);
            for (size_t i = 0; i < n; ++i) {
                const char *key = nullptr;
                size_t len = 0;
                chupa_object_key_at(CHUPA_W &v, i, &key, &len);
                benchmark::DoNotOptimize(key);
                ChupaValue item{};
                chupa_object_value_at(CHUPA_W &v, i, &item);
                consume(ctx, item);
            }
            return;
        }
    }
}

void runEvalAndDeliver(benchmark::State &state, std::string_view source) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr || !furnish(ctx)) {
        state.SkipWithError("furnish failed");
        return;
    }
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
        consume(ctx, out);
    }

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

/// Регистрация обоих блоков из одного набора: разойтись они не могут.
struct RegisterForms {
    RegisterForms() {
        for (const Form &form : kForms) {
            const std::string_view source = form.source;
            benchmark::RegisterBenchmark(
                std::string("BM_Host_Compile/") + form.name,
                [source](benchmark::State &s) { runCompile(s, source); });
            benchmark::RegisterBenchmark(
                std::string("BM_Host_Eval/") + form.name,
                [source](benchmark::State &s) { runEvalAndDeliver(s, source); });
        }
    }
};
const RegisterForms g_registerForms;

// ══════════════════════════════════════════════════════════════════════════
// Блок 4. Скрипты: обработчики действий.
// ══════════════════════════════════════════════════════════════════════════
//
// Компилируются раз на экран, выполняются по нажатию — обе половины меряют
// задержку, и требования к ним мягче всех. Но именно скрипт строит объекты и
// растит массивы, то есть бьёт по самой подорожавшей части модели.
//
// Каждый исходник обязан быть идемпотентным: цикл замера выполняет его
// миллионы раз, и неидемпотентный уходил бы всё дальше от начальных данных.
// Отсюда push в паре с pop и присваивание существующему ключу.

constexpr Form kScripts[] = {
    {"assign_field",  "user.age = 31;"},
    {"assign_deep",   "items[3].price = 17;"},
    {"assign_string", "user.first = 'Петя';"},
    {"compound",      "user.age += 0;"},
    {"push_pop",      "push(items, 1); pop(items);"},
    {"build_object",  "user.card = {'id': 1, 'name': 'Вася', 'active': true};"},
    {"build_string",  "user.full = format('${} ${}', user.first, user.last);"},
    {"five_assigns",  "user.a = 1; user.b = 2; user.c = 3; user.d = 4;"
                      " user.e = 5;"},
};

void runScriptCompile(benchmark::State &state, std::string_view source) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr || !furnish(ctx)) {
        state.SkipWithError("furnish failed");
        return;
    }
    state.SetLabel(std::string(source));
    for (auto _ : state) {
        ChupaScript *s = chupa_compile_script(ctx, source.data(), source.size());
        if (s == nullptr) {
            state.SkipWithError(errorMessage(ctx));
            break;
        }
        chupa_script_destroy(s);
    }
    chupa_context_destroy(ctx);
}

void runScriptRun(benchmark::State &state, std::string_view source) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr || !furnish(ctx)) {
        state.SkipWithError("furnish failed");
        return;
    }
    ChupaScript *s = chupa_compile_script(ctx, source.data(), source.size());
    if (s == nullptr) {
        state.SkipWithError(errorMessage(ctx));
        chupa_context_destroy(ctx);
        return;
    }
    state.SetLabel(std::string(source));
    for (auto _ : state) {
        if (!chupa_run(ctx, s)) {
            state.SkipWithError(errorMessage(ctx));
            break;
        }
    }
    chupa_script_destroy(s);
    chupa_context_destroy(ctx);
}

struct RegisterScripts {
    RegisterScripts() {
        for (const Form &form : kScripts) {
            const std::string_view source = form.source;
            benchmark::RegisterBenchmark(
                std::string("BM_Host_ScriptCompile/") + form.name,
                [source](benchmark::State &s) { runScriptCompile(s, source); });
            benchmark::RegisterBenchmark(
                std::string("BM_Host_ScriptRun/") + form.name,
                [source](benchmark::State &s) { runScriptRun(s, source); });
        }
    }
};
const RegisterScripts g_registerScripts;

// ══════════════════════════════════════════════════════════════════════════
// Блок 5. Кадр целиком.
// ══════════════════════════════════════════════════════════════════════════
//
// Одно число, по которому и принимается решение о модели; остальные блоки
// объясняют, откуда оно взялось.
//
// Состав: экран из двадцати вьюшек по четыре пропса-выражения. Выражения
// компилируются один раз, вне цикла, — так и делает хост. В кадре остаются
// восемьдесят вычислений с доставкой.
//
// Приход данных вынесен отдельной строкой (BM_Host_FrameWithData): обновление
// коллекции случается не на каждом кадре, и смешивать их в одну цифру значило
// бы предрешить пропорцию, которой мы не знаем.

constexpr std::string_view kProps[] = {
    "items[3].label",
    "format('${} ₽', items[3].price)",
    "width > 200 ? items[3].label : 'узко'",
    "user.nickname ?? 'Гость'",
};

constexpr int kViews = 20;

/// Компилирует восемьдесят пропсов экрана.
std::vector<ChupaExpression *> compileScreen(ChupaContext *ctx) {
    std::vector<ChupaExpression *> out;
    out.reserve(kViews * 4);
    for (int view = 0; view < kViews; ++view) {
        for (std::string_view prop : kProps) {
            out.push_back(
                chupa_compile_expression(ctx, prop.data(), prop.size()));
        }
    }
    return out;
}

void destroyScreen(std::vector<ChupaExpression *> &screen) {
    for (ChupaExpression *e : screen) { chupa_expression_destroy(e); }
}

/// Кадр: восемьдесят вычислений с доставкой, без прихода данных.
void BM_Host_Frame(benchmark::State &state) {
    ChupaContext *ctx = chupa_context_create();
    state.SetLabel("20 вьюшек x 4 пропса = 80 вычислений");
    if (ctx == nullptr || !furnish(ctx)) {
        state.SkipWithError("furnish failed");
        return;
    }
    std::vector<ChupaExpression *> screen = compileScreen(ctx);
    for (ChupaExpression *e : screen) {
        if (e == nullptr) {
            state.SkipWithError("compile failed");
            destroyScreen(screen);
            chupa_context_destroy(ctx);
            return;
        }
    }

    for (auto _ : state) {
        for (ChupaExpression *e : screen) {
            ChupaValue out{};
            if (chupa_eval(ctx, e, &out)) { consume(ctx, out); }
        }
    }

    destroyScreen(screen);
    chupa_context_destroy(ctx);
}
BENCHMARK(BM_Host_Frame);

/// Приход данных плюс кадр: так выглядит обновление ленты.
void BM_Host_FrameWithData(benchmark::State &state) {
    state.SetLabel("приход ленты из 20 карточек + 80 вычислений");
    static const std::string cards = cardList(20);
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr || !furnish(ctx)) {
        state.SkipWithError("furnish failed");
        return;
    }
    std::vector<ChupaExpression *> screen = compileScreen(ctx);

    for (auto _ : state) {
        if (!put(ctx, "items", cards)) {
            state.SkipWithError("set failed");
            break;
        }
        for (ChupaExpression *e : screen) {
            ChupaValue out{};
            if (chupa_eval(ctx, e, &out)) { consume(ctx, out); }
        }
    }

    destroyScreen(screen);
    chupa_context_destroy(ctx);
}
BENCHMARK(BM_Host_FrameWithData);

/// Открытие экрана: компиляция восьмидесяти пропсов на уже поставленных
/// данных. Раз на экран, но целиком в задержке первого кадра.
void BM_Host_ScreenCompile(benchmark::State &state) {
    ChupaContext *ctx = chupa_context_create();
    state.SetLabel("80 компиляций пропсов");
    if (ctx == nullptr || !furnish(ctx)) {
        state.SkipWithError("furnish failed");
        return;
    }
    for (auto _ : state) {
        std::vector<ChupaExpression *> screen = compileScreen(ctx);
        destroyScreen(screen);
    }
    chupa_context_destroy(ctx);
}
BENCHMARK(BM_Host_ScreenCompile);

}  // namespace
