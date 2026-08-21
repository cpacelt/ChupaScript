// Замеры кэша выражений — задача 10 (docs/superpowers/specs/2026-08-20-
// expression-cache-design.md, §5). Состав зафиксирован спекой ДО первого
// прогона: шесть строк (§5.1), три режима на каждую (§5.2) плюс базовая линия
// без кэша, и всё это меряется всегда — отбирать после прогона нечего.
//
// Читатель здесь — не CachedExpression<T> из Sources/ChupaScript, а его точная
// копия арифметики на C++ (класс CacheReader ниже): сумма CS::kMaxDeps слов
// без ветвления, безусловные retain/release владельцев набора. Причина не
// заимствовать сам Swift-класс тривиальна — он живёт в другом языке; причина
// не срезать угол в арифметике — §5 требует того же самого приёма, каким
// реально пользуется хост, а не облегчённой версии.
#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "box.hpp"
#include "context.hpp"
#include "diagnostic.hpp"
#include "epoch.hpp"
#include "expression.hpp"
#include "script.hpp"
#include "value.hpp"

namespace {

using CS::Context;
using CS::Dep;
using CS::Diagnostic;
using CS::Expression;
using CS::Script;
using CS::Value;

/// Данные шести строк из §5.1 разом: скаляр, объект с полем, массив объектов,
/// число, пятиуровневый объект (для переполнения) и четыре "прочих" переменных
/// экрана — timer/progress/scrollY/counter, — нужные только режиму Mixed
/// (см. runCacheMixed), чтобы пул записи не был вырожденным в один элемент.
bool fillScreen(Context &ctx) {
    Diagnostic diag;
    return ctx.setVariableText("button_enabled", "true", diag) &&
           ctx.setVariableText("user", "{'name': 'Вася'}", diag) &&
           ctx.setVariableText("users",
                               "[{'name': 'Вася'}, {'name': 'Петя'}]", diag) &&
           ctx.setVariableText("a", "1", diag) &&
           ctx.setVariableText(
               "u", "{'a': {'b': {'c': {'d': {'e': 1}}}}}", diag) &&
           ctx.setVariableText("timer", "0", diag) &&
           ctx.setVariableText("progress", "0", diag) &&
           ctx.setVariableText("scrollY", "0", diag) &&
           ctx.setVariableText("counter", "0", diag);
}

/// Исход одного чтения через кэш: попадание, промах (пришлось войти в
/// движок) или отказ вычисления. Отказ отдельно от промаха, потому что этот
/// бенчмарк не ожидает ни одного отказа на своих собственных данных — если
/// он случился, это дефект замера, а не результат.
enum class ReadResult { Hit, Miss, Failed };

/// Один "кадр записи" устройства потребителя. Два вида — выбор замера, не
/// принуждение языка: скрипт сегодня умеет присвоить самому имени глобальной
/// переменной (`button_enabled = true;` — законно, `docs/semantics.md` §7.2),
/// но у сценария BDUI для скаляра всё равно есть дверь хоста — обновление
/// пришедшего значения — и дверь программы — присваивание внутри обработчика,
/// и это две разные дороги к одной и той же ячейке. Поэтому скаляр здесь
/// меняет хост напрямую — Context::setGlobal с готовым Value, тем же
/// типизированным путём, каким реальный BDUI пушит новые props (а не
/// setVariableText: тот заново гоняет значение через разбор текста, и мерил
/// бы стоимость парсера, а не запись), — а путь внутрь объекта меняет скрипт,
/// тем же путём, каким его меняет обработчик. Оба вида бьют по эпохе, которую
/// видит evalTracked; разница только в том, чья дверь записи это делает.
class Writer {
   public:
    static Writer hostWrite(std::string name, Value value) {
        Writer w;
        w.kind_ = Kind::Host;
        w.name_ = std::move(name);
        w.value_ = value;
        return w;
    }

    static Writer script(std::string source) {
        Writer w;
        w.kind_ = Kind::Script;
        w.source_ = std::move(source);
        return w;
    }

    /// Компилирует скрипт (у хост-записи компилировать нечего). Один раз, до
    /// таймера.
    bool prepare(Context &ctx) {
        if (kind_ != Kind::Script) { return true; }
        Diagnostic diags[1];
        return ctx.compileScript(source_, &compiled_, diags, 1) == 0;
    }

    bool apply(Context &ctx, Diagnostic &diag) {
        if (kind_ == Kind::Host) {
            ctx.setGlobal(name_, value_);
            return true;
        }
        return ctx.run(compiled_, diag);
    }

   private:
    enum class Kind { Host, Script } kind_ = Kind::Host;
    std::string name_;
    Value value_ = Value::null();
    std::string source_;
    Script compiled_;
};

/// Точная копия арифметики CachedExpression.swift на C++ (задача 10, §5):
/// сумма CS::kMaxDeps слов без ветвления по числу занятых слотов, безусловные
/// retain/release на владельцах набора зависимостей.
///
/// Копия именно АРИФМЕТИКИ, а не удержания результата, и в одном месте
/// расходится с оригиналом намеренно: `cached_ = *out` ниже удерживает
/// четырёх владельцев набора, но не само значение. У строкового результата
/// это провисшая ручка после ближайшей границы операции — Swift-обёртка
/// такого не допускает, она хранит уже собранный String. На числа замера это
/// не влияет: на попадании `cached_` только копируется наружу и ни разу не
/// разыменовывается, а меряется здесь цена ПРОВЕРКИ (сумма эпох против
/// снимка), которая от содержимого ручки не зависит вовсе. Удержание не
/// заводится потому, что оно добавило бы к горячему пути retain/release,
/// которых у настоящего читателя на попадании нет, — то есть исказило бы
/// ровно то число, ради которого этот класс написан.
class CacheReader {
   public:
    ~CacheReader() { releaseOwners(); }

    ReadResult read(Context &ctx, const Expression &expr, Value *out,
                    Diagnostic &diag) {
        if (hasValue_ && !uncacheable_ && sum() == snapshot_) {
            *out = cached_;
            return ReadResult::Hit;
        }

        Dep fresh[CS::kMaxDeps];
        std::uint32_t n = 0;
        if (!ctx.evalTracked(expr, out, fresh, &n, diag)) {
            return ReadResult::Failed;
        }

        releaseOwners();
        uncacheable_ = (n == CS::kDepsOverflow);
        if (uncacheable_) {
            // Контракт заголовка: на переполнении каждый fresh[i].epoch —
            // nullptr, поэтому набор читателя явно сбрасывается на
            // собственный вечный ноль, а не копируется как есть — ровно как
            // в capture() у CachedExpression.swift.
            for (auto &d : deps_) { d = Dep{}; }
        } else {
            for (std::uint32_t i = 0; i < CS::kMaxDeps; ++i) {
                CS::detail::retainValue(fresh[i].owner);
            }
            for (std::uint32_t i = 0; i < CS::kMaxDeps; ++i) {
                deps_[i] = fresh[i];
            }
            snapshot_ = sum();
        }
        cached_ = *out;
        hasValue_ = true;
        return ReadResult::Miss;
    }

   private:
    [[nodiscard]] CS::Epoch sum() const noexcept {
        CS::Epoch total = 0;
        for (std::uint32_t i = 0; i < CS::kMaxDeps; ++i) {
            total += *deps_[i].epoch;
        }
        return total;
    }

    void releaseOwners() {
        if (!hasValue_ || uncacheable_) { return; }
        for (std::uint32_t i = 0; i < CS::kMaxDeps; ++i) {
            CS::detail::releaseValue(deps_[i].owner);
        }
    }

    Dep deps_[CS::kMaxDeps];
    CS::Epoch snapshot_ = 0;
    Value cached_ = Value::null();
    bool hasValue_ = false;
    bool uncacheable_ = false;
};

/// Пишет долю попаданий рядом с числом (§5.2.3, §5.4): число без указанной
/// доли — не результат.
void publishHitRate(benchmark::State &state, std::uint64_t iters,
                    std::uint64_t misses) {
    state.counters["hit_rate"] =
        iters ? static_cast<double>(iters - misses) / static_cast<double>(iters)
             : 0.0;
}

// ─── Режим 1: все попадания — верхняя граница выигрыша (§5.2.1) ───────────

void runCacheAllHits(benchmark::State &state, std::string_view source) {
    Context ctx;
    if (!fillScreen(ctx)) { state.SkipWithError("fillScreen failed"); return; }

    Expression expr;
    Diagnostic diags[1];
    if (ctx.compileExpression(source, &expr, diags, 1) != 0) {
        state.SkipWithError("compile failed");
        return;
    }

    CacheReader reader;
    Diagnostic diag;
    std::uint64_t misses = 0, iters = 0;
    for (auto _ : state) {
        Value out = Value::null();
        ReadResult r = reader.read(ctx, expr, &out, diag);
        if (r == ReadResult::Failed) {
            state.SkipWithError("evalTracked failed");
            return;
        }
        if (r == ReadResult::Miss) { ++misses; }
        ++iters;
        benchmark::DoNotOptimize(out);
    }
    publishHitRate(state, iters, misses);
}

// ─── Режим 2: ни одного попадания — враждебный (§5.2.2) ───────────────────
//
// Каждый кадр пишет переменную, от которой зависит выражение — ту же роль в
// реальном приложении играют прогресс анимации или таймер: что-то меняется
// каждый кадр, и кэш платит проверку, не получая ничего взамен. Для `42`
// пишется переменная, от которой это выражение НЕ зависит: у константы
// набор зависимостей пуст, и никакая запись извне его не заденет — этот факт
// стоит того, чтобы быть видимым в отчёте, а не спрятанным подменой строки.
//
// У этого режима есть безкэшевая пара — runCacheBaselineNoHits ниже — та же
// запись, тем же писателем, на каждой же итерации, только вместо кэша прямой
// ctx.eval. Без неё «дороже кэш или дешевле» неотличимо от «дороже одна
// запись сама по себе»: запись — работа хоста, она идёт что с кэшем, что без
// него, и не должна целиком записываться в счёт кэша (§5.4: сравнение на
// одном дереве и одних данных, а значит и при одной и той же прочей работе).

void runCacheNoHits(benchmark::State &state, std::string_view source,
                    Writer writer) {
    Context ctx;
    if (!fillScreen(ctx)) { state.SkipWithError("fillScreen failed"); return; }

    Expression expr;
    Diagnostic diags[1];
    if (ctx.compileExpression(source, &expr, diags, 1) != 0) {
        state.SkipWithError("compile failed");
        return;
    }

    if (!writer.prepare(ctx)) {
        state.SkipWithError("writer prepare failed");
        return;
    }

    CacheReader reader;
    Diagnostic diag;
    std::uint64_t misses = 0, iters = 0;
    for (auto _ : state) {
        Diagnostic wdiag;
        if (!writer.apply(ctx, wdiag)) {
            state.SkipWithError("write failed");
            return;
        }
        Value out = Value::null();
        ReadResult r = reader.read(ctx, expr, &out, diag);
        if (r == ReadResult::Failed) {
            state.SkipWithError("evalTracked failed");
            return;
        }
        if (r == ReadResult::Miss) { ++misses; }
        ++iters;
        benchmark::DoNotOptimize(out);
    }
    publishHitRate(state, iters, misses);
}

/// Безкэшевая пара для NoHits: та же запись, тем же писателем, на каждой же
/// итерации, вместо кэша — прямой ctx.eval. Изолирует цену промаха от цены
/// самой записи (см. комментарий у runCacheNoHits).
void runCacheBaselineNoHits(benchmark::State &state, std::string_view source,
                            Writer writer) {
    Context ctx;
    if (!fillScreen(ctx)) { state.SkipWithError("fillScreen failed"); return; }

    Expression expr;
    Diagnostic diags[1];
    if (ctx.compileExpression(source, &expr, diags, 1) != 0) {
        state.SkipWithError("compile failed");
        return;
    }

    if (!writer.prepare(ctx)) {
        state.SkipWithError("writer prepare failed");
        return;
    }

    Diagnostic diag;
    for (auto _ : state) {
        Diagnostic wdiag;
        if (!writer.apply(ctx, wdiag)) {
            state.SkipWithError("write failed");
            return;
        }
        Value out = Value::null();
        if (!ctx.eval(expr, &out, diag)) {
            state.SkipWithError("eval failed");
            return;
        }
        benchmark::DoNotOptimize(out);
    }
}

// ─── Режим 3: смесь — доля попаданий из устройства потребителя (§5.2.3) ───
//
// У бенчмарка нет доступа к телеметрии живого экрана, поэтому "устройство
// потребителя" смоделировано явно: восемь переменных типичного экрана,
// запись идёт по кругу равномерно. Доля попаданий НЕ назначена числом — она
// следует из того, сколько из восьми переменных пула действительно
// затрагивает зависимости конкретного выражения, и печатается в hit_rate по
// факту прогона, а не заранее.
//
// §1/§5.2.3 описывают устройство потребителя так: одна запись — полная
// раскладка — чтение ВСЕХ выражений экрана, а не одно выражение на одну
// запись. Первая версия этого файла читала ровно одно выражение на каждую
// запись — цена записи оказывалась полностью на счету однократного чтения,
// вместо того чтобы размазаться по всей раскладке, как в реальном кадре.
// Здесь запись пула происходит не на каждой итерации, а раз в kLayoutSize
// итераций — kLayoutSize смоделирован как размер "раскладки" в шесть
// выражений, зафиксированных составом §5.1 (сам список строк отчёта и есть
// модельный экран: одна запись, потом шесть чтений, потом снова запись).
// Это по-прежнему модель, а не телеметрия — оговорка называется явно, как
// того требует правило "число без оговорки не число".
constexpr std::size_t kLayoutSize = 6;

struct ScreenPool {
    std::vector<Writer> writers;
};

ScreenPool makeScreenPool() {
    ScreenPool pool;
    pool.writers.push_back(
        Writer::hostWrite("button_enabled", Value::boolean(true)));       // 0
    pool.writers.push_back(
        Writer::script("user.name = 'Аня';"));               // 1
    pool.writers.push_back(
        Writer::script("users[0].name = 'Боря';"));          // 2
    pool.writers.push_back(Writer::hostWrite("a", Value::number(5)));      // 3
    pool.writers.push_back(Writer::hostWrite("timer", Value::number(1)));  // 4: посторонняя
    pool.writers.push_back(
        Writer::hostWrite("progress", Value::number(1)));                  // 5: посторонняя
    pool.writers.push_back(
        Writer::hostWrite("scrollY", Value::number(1)));                   // 6: посторонняя
    pool.writers.push_back(
        Writer::hostWrite("counter", Value::number(1)));                   // 7: посторонняя
    return pool;
}

bool preparePool(Context &ctx, ScreenPool &pool) {
    for (Writer &w : pool.writers) {
        if (!w.prepare(ctx)) { return false; }
    }
    return true;
}

void runCacheMixed(benchmark::State &state, std::string_view source) {
    Context ctx;
    if (!fillScreen(ctx)) { state.SkipWithError("fillScreen failed"); return; }

    Expression expr;
    Diagnostic diags[1];
    if (ctx.compileExpression(source, &expr, diags, 1) != 0) {
        state.SkipWithError("compile failed");
        return;
    }

    ScreenPool pool = makeScreenPool();
    if (!preparePool(ctx, pool)) {
        state.SkipWithError("pool prepare failed");
        return;
    }

    CacheReader reader;
    Diagnostic diag;
    std::size_t writeIdx = 0;
    std::uint64_t misses = 0, iters = 0;
    for (auto _ : state) {
        if (iters % kLayoutSize == 0) {
            Diagnostic wdiag;
            if (!pool.writers[writeIdx % pool.writers.size()].apply(ctx,
                                                                     wdiag)) {
                state.SkipWithError("write failed");
                return;
            }
            ++writeIdx;
        }
        Value out = Value::null();
        ReadResult r = reader.read(ctx, expr, &out, diag);
        if (r == ReadResult::Failed) {
            state.SkipWithError("evalTracked failed");
            return;
        }
        if (r == ReadResult::Miss) { ++misses; }
        ++iters;
        benchmark::DoNotOptimize(out);
    }
    publishHitRate(state, iters, misses);
}

/// Безкэшевая пара для Mixed: тот же пул, то же расписание записи (раз в
/// kLayoutSize итераций, та же последовательность писателей от нуля), вместо
/// кэша — прямой ctx.eval на каждой итерации. Изолирует цену смеси от цены
/// записи, которая идёт одинаково по обе стороны сравнения.
void runCacheBaselineMixed(benchmark::State &state, std::string_view source) {
    Context ctx;
    if (!fillScreen(ctx)) { state.SkipWithError("fillScreen failed"); return; }

    Expression expr;
    Diagnostic diags[1];
    if (ctx.compileExpression(source, &expr, diags, 1) != 0) {
        state.SkipWithError("compile failed");
        return;
    }

    ScreenPool pool = makeScreenPool();
    if (!preparePool(ctx, pool)) {
        state.SkipWithError("pool prepare failed");
        return;
    }

    Diagnostic diag;
    std::size_t writeIdx = 0;
    std::uint64_t iters = 0;
    for (auto _ : state) {
        if (iters % kLayoutSize == 0) {
            Diagnostic wdiag;
            if (!pool.writers[writeIdx % pool.writers.size()].apply(ctx,
                                                                     wdiag)) {
                state.SkipWithError("write failed");
                return;
            }
            ++writeIdx;
        }
        Value out = Value::null();
        if (!ctx.eval(expr, &out, diag)) {
            state.SkipWithError("eval failed");
            return;
        }
        ++iters;
        benchmark::DoNotOptimize(out);
    }
}

// ─── Базовая линия: то же дерево, те же данные, без кэша (§5.4) ───────────

void runCacheBaseline(benchmark::State &state, std::string_view source) {
    Context ctx;
    if (!fillScreen(ctx)) { state.SkipWithError("fillScreen failed"); return; }

    Expression expr;
    Diagnostic diags[1];
    if (ctx.compileExpression(source, &expr, diags, 1) != 0) {
        state.SkipWithError("compile failed");
        return;
    }

    Diagnostic diag;
    for (auto _ : state) {
        Value out = Value::null();
        if (!ctx.eval(expr, &out, diag)) {
            state.SkipWithError("eval failed");
            return;
        }
        benchmark::DoNotOptimize(out);
    }
}

// ─── Шесть строк, зафиксированные §5.1 ─────────────────────────────────────

#define CHUPA_CACHE_ROW(Name, Source, WriterExpr)                            \
    void BM_Cache_##Name##_AllHits(benchmark::State &state) {                \
        runCacheAllHits(state, Source);                                     \
    }                                                                       \
    BENCHMARK(BM_Cache_##Name##_AllHits);                                   \
    void BM_Cache_##Name##_NoHits(benchmark::State &state) {                 \
        runCacheNoHits(state, Source, WriterExpr);                          \
    }                                                                       \
    BENCHMARK(BM_Cache_##Name##_NoHits);                                    \
    void BM_Cache_##Name##_Baseline_NoHits(benchmark::State &state) {        \
        runCacheBaselineNoHits(state, Source, WriterExpr);                  \
    }                                                                       \
    BENCHMARK(BM_Cache_##Name##_Baseline_NoHits);                           \
    void BM_Cache_##Name##_Mixed(benchmark::State &state) {                  \
        runCacheMixed(state, Source);                                       \
    }                                                                       \
    BENCHMARK(BM_Cache_##Name##_Mixed);                                     \
    void BM_Cache_##Name##_Baseline_Mixed(benchmark::State &state) {         \
        runCacheBaselineMixed(state, Source);                               \
    }                                                                       \
    BENCHMARK(BM_Cache_##Name##_Baseline_Mixed);                            \
    void BM_Cache_##Name##_Baseline(benchmark::State &state) {               \
        runCacheBaseline(state, Source);                                    \
    }                                                                       \
    BENCHMARK(BM_Cache_##Name##_Baseline)

/// `42` — набор пуст, попадание вечное. NoHits пишет постороннюю переменную
/// намеренно: это выражение не зависит ни от чего, и это должно быть видно.
CHUPA_CACHE_ROW(Constant, "42", Writer::hostWrite("timer", Value::number(1)));

/// `button_enabled` — голый скаляр, массовый props. Записывает хост
/// (setVariableText), не скрипт: скрипту нельзя присвоить самому имени
/// (docs/semantics.md §7.2).
CHUPA_CACHE_ROW(Scalar, "button_enabled",
               Writer::hostWrite("button_enabled", Value::boolean(true)));

/// `user.name` — один сегмент, самый частый осмысленный props.
CHUPA_CACHE_ROW(ShortPath, "user.name", Writer::script("user.name = 'Аня';"));

/// `users[0].name` — индекс плюс поле.
CHUPA_CACHE_ROW(IndexedPath, "users[0].name",
               Writer::script("users[0].name = 'Боря';"));

/// `a > 0 && user.name != ''` — две переменные и путь. Пишет одну из двух
/// зависимостей (`user.name`) — этого достаточно, чтобы задеть сумму целиком.
CHUPA_CACHE_ROW(LogicalPath, "a > 0 && user.name != ''",
               Writer::script("user.name = 'Аня';"));

/// `u.a.b.c.d.e` — сверх потолка: не кэшируется, платит проверку впустую.
/// Обязательная строка (§5.1): без неё замер отвечает не на тот вопрос.
CHUPA_CACHE_ROW(Overflow, "u.a.b.c.d.e", Writer::script("u.a.b.c.d.e = 2;"));

#undef CHUPA_CACHE_ROW

// ─── Вытесненный кэш процессора (§5.3) ─────────────────────────────────────
//
// Подозреваемый на ухудшение — обращение к заголовку ObjectBox, к которому на
// попадании иначе не пришли бы вовсе (§4.3). На горячем кэше (один Context,
// один и тот же адрес на каждой итерации) этого не увидеть: адрес уже в L1.
// Здесь вместо одного дерева — kDisplacedTrees независимых контекстов с
// одинаковыми данными, обход по кругу, а не прогулка по буферу (тот же приём,
// что в layout-synthetics-2026-08-18: вращение множества деревьев). Между
// прогревом и измерением ничего не пишется — это по-прежнему режим AllHits,
// проверяется только то, что чтение при попадании стоит дороже, когда данные
// не в кэше процессора.
constexpr int kDisplacedTrees = 4096;

struct DisplacedTree {
    std::unique_ptr<Context> ctx;
    Expression expr;
    CacheReader reader;
};

void runCacheDisplacedAllHits(benchmark::State &state, std::string_view source) {
    std::vector<DisplacedTree> trees(kDisplacedTrees);
    for (auto &t : trees) {
        t.ctx = std::make_unique<Context>();
        if (!fillScreen(*t.ctx)) {
            state.SkipWithError("fillScreen failed");
            return;
        }
        Diagnostic diags[1];
        if (t.ctx->compileExpression(source, &t.expr, diags, 1) != 0) {
            state.SkipWithError("compile failed");
            return;
        }
        // Первый промах — вне таймера: интересует цена ПОПАДАНИЯ на холодной
        // памяти, а не цена первого разбора набора.
        Value out = Value::null();
        Diagnostic diag;
        if (t.reader.read(*t.ctx, t.expr, &out, diag) == ReadResult::Failed) {
            state.SkipWithError("warmup failed");
            return;
        }
    }

    std::size_t idx = 0;
    Diagnostic diag;
    std::uint64_t misses = 0, iters = 0;
    for (auto _ : state) {
        DisplacedTree &t = trees[idx % trees.size()];
        ++idx;
        Value out = Value::null();
        ReadResult r = t.reader.read(*t.ctx, t.expr, &out, diag);
        if (r == ReadResult::Failed) {
            state.SkipWithError("evalTracked failed");
            return;
        }
        if (r == ReadResult::Miss) { ++misses; }
        ++iters;
        benchmark::DoNotOptimize(out);
    }
    publishHitRate(state, iters, misses);
}

/// Скаляр — §4.3 подозревает, что у него разницы нет вовсе (эпоха ячейки
/// лежит в EpochSlots, подряд, без обращения к заголовку коробки).
void BM_Cache_Displaced_Scalar_AllHits(benchmark::State &state) {
    runCacheDisplacedAllHits(state, "button_enabled");
}
BENCHMARK(BM_Cache_Displaced_Scalar_AllHits);

/// Один сегмент — путь, у которого попадание трогает заголовок ObjectBox.
void BM_Cache_Displaced_ShortPath_AllHits(benchmark::State &state) {
    runCacheDisplacedAllHits(state, "user.name");
}
BENCHMARK(BM_Cache_Displaced_ShortPath_AllHits);

/// Индекс плюс поле — два заголовка (ArrayBox и ObjectBox) на попадании.
void BM_Cache_Displaced_IndexedPath_AllHits(benchmark::State &state) {
    runCacheDisplacedAllHits(state, "users[0].name");
}
BENCHMARK(BM_Cache_Displaced_IndexedPath_AllHits);

// ─── Потолок: сколько выражений реального экрана перестаёт переполняться ──
//
// §9 спеки решается по двум числам: сколько строк этого корпуса перестаёт
// переполняться при восьми зависимостях вместо четырёх, и на сколько
// подорожала проверка у массового скаляра (см. BM_Cache_Scalar_AllHits —
// то же число, но из другого билда). У бенчмарка нет телеметрии живого
// экрана, поэтому корпус ниже смоделирован — двенадцать props разной формы,
// какие встречаются в BDUI пачках (скаляры, пути, индексы, композиты,
// глубокие цепочки) — а не выписан из живого дампа. Это прямо соответствует
// правилу отчёта "число без оговорки — не число": оговорка здесь и есть.
bool fillCorpus(Context &ctx) {
    Diagnostic diag;
    return ctx.setVariableText("button_enabled", "true", diag) &&
           ctx.setVariableText("is_visible", "true", diag) &&
           ctx.setVariableText(
               "user",
               "{'name': 'Вася', 'avatar': {'url': 'a.png'}, 'profile': "
               "{'city': {'code': {'zip': 101000}}}}",
               diag) &&
           ctx.setVariableText(
               "users", "[{'name': 'Вася'}]", diag) &&
           ctx.setVariableText(
               "cart", "{'items': [{'price': 100}]}", diag) &&
           ctx.setVariableText("a", "1", diag) &&
           ctx.setVariableText(
               "settings",
               "{'theme': {'colors': {'primary': '#000'}}}", diag) &&
           ctx.setVariableText(
               "order",
               "{'items': [{'product': {'title': 'X'}}]}", diag) &&
           ctx.setVariableText(
               "form", "{'fields': [0, 0, {'value': {'trimmed': 'a'}}]}",
               diag) &&
           ctx.setVariableText(
               "user2",
               "{'address': {'city': {'district': {'zone': "
               "{'code': 1}}}}}",
               diag);
}

/// Двенадцать props корпуса: см. комментарий выше fillCorpus. Каждая строка
/// — реалистичная форма биндинга, не выдуманная под конкретный потолок.
constexpr std::array<std::string_view, 12> kCorpus = {
    "button_enabled",
    "is_visible",
    "user.name",
    "user.avatar.url",
    "users[0].name",
    "cart.items[0].price",
    "a > 0 && user.name != ''",
    "user.profile.city.code.zip",
    "settings.theme.colors.primary",
    "order.items[0].product.title",
    "form.fields[2].value.trimmed",
    "user2.address.city.district.zone.code",
};

/// Не измерение времени — снимок состояния корпуса при текущем CS::kMaxDeps.
/// Считается один раз (репетиции дают одно и то же число — детерминировано),
/// counters несут единственный ответ, который здесь нужен: сколько строк
/// корпуса переполнилось при действующем потолке.
void BM_Cache_CeilingCorpus(benchmark::State &state) {
    Context ctx;
    if (!fillCorpus(ctx)) { state.SkipWithError("fillCorpus failed"); return; }

    for (auto _ : state) {
        std::uint32_t overflowed = 0;
        for (std::string_view source : kCorpus) {
            Expression expr;
            Diagnostic diags[1];
            if (ctx.compileExpression(source, &expr, diags, 1) != 0) {
                state.SkipWithError("corpus compile failed");
                return;
            }
            Value out = Value::null();
            Dep deps[CS::kMaxDeps];
            std::uint32_t n = 0;
            Diagnostic diag;
            if (!ctx.evalTracked(expr, &out, deps, &n, diag)) {
                state.SkipWithError("corpus eval failed");
                return;
            }
            if (n == CS::kDepsOverflow) { ++overflowed; }
            benchmark::DoNotOptimize(out);
        }
        state.counters["overflow_count"] = overflowed;
        state.counters["corpus_size"] = static_cast<double>(kCorpus.size());
        state.counters["max_deps"] = static_cast<double>(CS::kMaxDeps);
    }
}
BENCHMARK(BM_Cache_CeilingCorpus);

}  // namespace
