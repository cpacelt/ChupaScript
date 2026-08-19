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
/// обёртки. Смысл появился, когда у пары завелось правило: список отложенного
/// освобождения сливается на границе операции, и у этого правила должно быть
/// ровно одно место. Оно здесь, поэтому мимо него не пройти.
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

    /// Вычисляет в значение любого вида, годное к удержанию хостом.
    ///
    /// Отличие от eval одно, и оно существенное: вычисленная строка
    /// материализуется в коробку. eval отдаёт результат как есть — им пользуется
    /// оболочка, которая читает его немедленно и до следующей операции. Здесь
    /// же результат предназначен к тому, чтобы его удержали, а удержать можно
    /// только коробка.
    ///
    /// Ссылки не берёт: единственную ссылку результата держит список
    /// отложенного освобождения, и ближайшая операция её отпустит. Хост,
    /// которому значение нужно дольше, обязан взять свою — chupa_value_retain.
    bool evalValue(const Expression &expr, Value *out, Diagnostic &diag);

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
    /// Операция, а не голая запись, и это обязательно: созданная здесь коробка
    /// попадает в список отложенного освобождения, и без границы список рос бы
    /// до конца жизни контекста. Хост, который только пишет и ни разу не
    /// вычисляет, — обычное дело на старте экрана.
    void setGlobal(std::string_view name, Value v) {
        // v may be all that is left of a value eval() handed back from an
        // earlier operation: its only reference is the creator reference
        // sitting in exec_.deferred_, and beginOperation() below is about to
        // drain exactly that list. Retaining first keeps the box alive
        // across the drain; the reference is deposited into the fresh list
        // right after, so it is not immortal — merely carried across this
        // one boundary, same as every other reference this method creates.
        detail::retainValue(v);
        beginOperation();
        exec_.deferred().take(v);
        store_.setGlobal(name, v, exec_.deferred());
    }

    /// Строка от хоста: укладывается коробкой, потому что переживёт операцию.
    void setGlobalString(std::string_view name, std::string_view text) {
        beginOperation();
        store_.setGlobal(name, CS::materialize(text, exec_.deferred()),
                         exec_.deferred());
    }

    /// Разбор текста от хоста в глобальную переменную. Тоже операция, и по той
    /// же причине: разбор создаёт коробки.
    bool setVariableText(std::string_view name, std::string_view text,
                         Diagnostic &diag);

    /// Выполняет скрипт. Значения у скрипта нет (docs/semantics.md §3.1).
    bool run(const Script &script, Diagnostic &diag);

    /// Compiles an expression against this Context's own Store.
    ///
    /// The only door to compilation: Expression::compile needs a mutable
    /// Store to intern the names check.hpp resolves, and store() below hands
    /// out a const view on purpose (defect Б2 — a caller that reached in
    /// through a mutable store() could write a global outside setGlobal's
    /// operation-boundary discipline, or hand Expression::compile a Store
    /// this Context does not own).
    [[nodiscard]] std::uint32_t compileExpression(std::string_view source,
                                                  Expression *out,
                                                  Diagnostic *diags,
                                                  std::uint32_t capacity) {
        return Expression::compile(source, store_, out, diags, capacity);
    }

    /// Same door, for a Script.
    [[nodiscard]] std::uint32_t compileScript(std::string_view source, Script *out,
                                              Diagnostic *diags,
                                              std::uint32_t capacity) {
        return Script::compile(source, store_, out, diags, capacity);
    }

    /// The Store, exposed: the shell (`:vars`, printValue) and the C API both
    /// need the name list and aggregate traversal it offers. Read-only only —
    /// defect Б2: this door used to hand out a mutable reference too, and
    /// through it a global could be written bypassing setGlobal, skipping
    /// this Context's operation boundary entirely. Mutation goes through
    /// setGlobal, setGlobalString, setVariableText; compilation goes through
    /// compileExpression and compileScript.
    [[nodiscard]] const Store &store() const noexcept { return store_; }

   private:
    /// Operation boundary: drains the deferred-release list.
    ///
    /// Drained at the start of the operation, not at the end: an eval's
    /// result may be a box whose only reference is held by the deferred
    /// list, and the caller reads that result right after the call returns.
    /// Draining on the way out would take the result away at the exact
    /// moment it is needed. The rule for the host is unchanged either way: a
    /// value stays valid only until the next operation; living longer than
    /// that requires taking a reference to it.
    ///
    /// Once per operation, not per block or loop iteration: `acc = acc +
    /// str(x)` inside a `for` holds a temporary that must survive the
    /// iteration. Garbage accumulating within a single operation is a cost
    /// this design tolerates — the same trade the watermark [B1] made
    /// (docs/backlog.md [B57]).
    void beginOperation() noexcept {
        exec_.deferred().drain();
    }

    Store store_;
    Execution exec_{store_};
};

}  // namespace CS
