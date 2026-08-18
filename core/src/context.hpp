#pragma once
#include <cstddef>
#include <string_view>

#include "diagnostic.hpp"
#include "execution.hpp"
#include "expression.hpp"
#include "script.hpp"
#include "store.hpp"
#include "value.hpp"

namespace CS {

/// Всё, что нужно, чтобы вычислять: постоянное хранилище и состояние
/// выполнения, связанные насовсем.
///
/// Пара появилась вместе с состоянием выполнения (docs/backlog.md [B57]). До
/// него контекст был одним `Store`, и класс вокруг него был бы обёрткой ради
/// обёртки. Смысл появился, когда у пары завелось правило: временный регион
/// сбрасывается на границе операции, и у этого правила должно быть ровно одно
/// место. Оно здесь, поэтому мимо него не пройти.
///
/// Скомпилированных единиц контекст не знает и не удерживает ([B35]): владеет
/// ими тот, кто их создал, а сюда они приходят на время вызова. Обратное
/// требование остаётся: единица годна только для того контекста, в хранилище
/// которого её скомпилировали — и номера ячеек, и уложенные литералы адресуют
/// его пулы.
///
/// Не копируется и не перемещается: значения адресуют пулы **этого**
/// хранилища, и второй экземпляр той же начинки означал бы два владельца
/// одних данных.
class Context {
   public:
    Context() = default;

    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    Context(Context &&) = delete;
    Context &operator=(Context &&) = delete;

    /// Вычисляет выражение. Форма исхода — как у Expression::eval
    /// (core/src/expression.hpp), включая то, что при отказе *out не трогается.
    bool eval(const Expression &expr, Value *out, Diagnostic &diag);

    /// Типизированные исходы — как у одноимённых методов Expression.
    EvalStatus evalNumber(const Expression &expr, double *out,
                          Diagnostic &diag);
    EvalStatus evalBool(const Expression &expr, bool *out, Diagnostic &diag);

    /// Срез действителен до следующей записи в текстовый пул — правило
    /// Expression::evalString целиком.
    EvalStatus evalString(const Expression &expr, std::string_view *out,
                          Diagnostic &diag);

    /// Выполняет скрипт. Значения у скрипта нет (docs/semantics.md §3.1).
    bool run(const Script &script, Diagnostic &diag);

    /// Хранилище наружу: состав имён и обход агрегатов нужны и оболочке
    /// (`:vars`, printValue), и C API (chupa_context_set*), и компиляции —
    /// прятать его не за чем. Состояние выполнения, наоборот, наружу не
    /// отдаётся: трогать его помимо eval и run незачем.
    [[nodiscard]] Store &store() noexcept { return store_; }
    [[nodiscard]] const Store &store() const noexcept { return store_; }

    /// Хранилище, которому принадлежит значение. Результат вычисления вправе
    /// лежать во временном регионе (`[1, 2]`, `format(...)`), и прочитать его
    /// может только тот, кто его выдал — см. storeOf в
    /// core/src/execution.hpp. Такое значение годно до следующей операции над
    /// контекстом.
    [[nodiscard]] const Store &storeOf(Value v) const noexcept {
        return exec_.storeOf(v);
    }

    /// Сколько байт занято временным регионом сейчас.
    ///
    /// Метрика, а не окно в состояние выполнения: наружу отдаётся число, а не
    /// хранилище. Нужна затем, что рост временного региона иначе ничем не
    /// виден — bytesUsed самого Store не выставлен ни в C API, ни в обёртке
    /// (docs/backlog.md [B57], «Нерешённое»).
    [[nodiscard]] std::size_t temporaryBytesUsed() const noexcept {
        return exec_.scratch.bytesUsed();
    }

   private:
    /// Граница операции: временный регион освобождается целиком.
    ///
    /// В начале операции, а не в конце: результат вычисления вправе лежать во
    /// временном регионе, и вызывающий читает его сразу после возврата. Сброс
    /// на выходе отнимал бы у него результат ровно в тот момент, когда он
    /// нужен.
    ///
    /// Раз на операцию, а не на блок или итерацию цикла: `acc = acc + str(x)`
    /// внутри `for` держит временное значение, обязанное пережить итерацию.
    /// Мусор внутри одной операции приходится терпеть — та же сделка, на
    /// которую шла водяная метка [B1] (docs/backlog.md [B57]).
    void beginOperation() noexcept { exec_.scratch.clear(); }

    Store store_;
    Execution exec_{store_};
};

}  // namespace CS
