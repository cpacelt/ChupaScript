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

    /// Запись глобальной переменной от хоста.
    ///
    /// Операция, а не голая запись, и это обязательно: созданный здесь узел
    /// попадает в список отложенного освобождения, и без границы список рос бы
    /// до конца жизни контекста. Хост, который только пишет и ни разу не
    /// вычисляет, — обычное дело на старте экрана.
    void setGlobal(std::string_view name, Value v) {
        beginOperation();
        store_.setGlobal(name, store_.promote(exec_.storeOf(v), v));
    }

    /// Строка от хоста: укладывается узлом, потому что переживёт операцию.
    void setGlobalString(std::string_view name, std::string_view text) {
        beginOperation();
        store_.setGlobal(name, store_.materialize(text));
    }

    /// Разбор текста от хоста в глобальную переменную. Тоже операция, и по той
    /// же причине: разбор создаёт узлы.
    bool setVariableText(std::string_view name, std::string_view text,
                         Diagnostic &diag);

    /// Выполняет скрипт. Значения у скрипта нет (docs/semantics.md §3.1).
    bool run(const Script &script, Diagnostic &diag);

    /// Хранилище наружу: состав имён и обход агрегатов нужны и оболочке
    /// (`:vars`, printValue), и C API (chupa_context_set*), и компиляции —
    /// прятать его не за чем. Состояние выполнения, наоборот, наружу не
    /// отдаётся: трогать его помимо eval и run незачем.
    [[nodiscard]] Store &store() noexcept { return store_; }
    [[nodiscard]] const Store &store() const noexcept { return store_; }

    /// Хранилище, способное прочитать значение.
    ///
    /// Роль сузилась до одного случая: агрегат и узел строки самодостаточны, а
    /// вот результат вычисления вправе быть промежуточной строкой
    /// (`format(...)`), и смысл её смещению придаёт только арена операции.
    /// Такое значение годно до следующей операции над контекстом.
    ///
    /// Наружу метод остаётся ради того, кто читает сырой результат eval, — это
    /// оболочка (cli/printer.cpp). Обёртка на Swift сюда не ходит: ей строка
    /// приходит срезом через evalString, и она копирует её немедленно.
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
    /// временном регионе либо быть узлом, чью единственную ссылку держит
    /// список отложенного освобождения, — а вызывающий читает результат сразу
    /// после возврата. Сброс на выходе отнимал бы его ровно в тот момент,
    /// когда он нужен. Правило для хоста от этого не изменилось: значение
    /// годно до следующей операции, а чтобы оно жило дольше — надо взять на
    /// него ссылку.
    ///
    /// Раз на операцию, а не на блок или итерацию цикла: `acc = acc + str(x)`
    /// внутри `for` держит временное значение, обязанное пережить итерацию.
    /// Мусор внутри одной операции приходится терпеть — та же сделка, на
    /// которую шла водяная метка [B1] (docs/backlog.md [B57]).
    void beginOperation() noexcept {
        exec_.scratch.clear();
        exec_.scratch.drainPending();
        store_.drainPending();
    }

    Store store_;
    Execution exec_{store_};
};

}  // namespace CS
