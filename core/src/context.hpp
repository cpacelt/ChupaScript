#pragma once
#include <cassert>
#include <cstddef>
#include <string_view>

#include "diagnostic.hpp"
#include "execution.hpp"
#include "expression.hpp"
#include "host.hpp"
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
/// которого её скомпилировали — номера ячеек адресуют его пулы.
///
/// Не копируется и не перемещается: значения адресуют пулы **этого**
/// хранилища, и второй экземпляр той же начинки означал бы два владельца
/// одних данных.
///
/// LAYOUT — everything this Context owns:
///
///   store_        Store — global variable names/values and the key table
///                 (store.hpp). Persists across every operation.
///   exec_         Execution — the format() string builder and the deferred-
///                 release list (execution.hpp), bound to store_ by
///                 reference for this Context's whole lifetime.
///   lastResult_   Value. A ROOT (Р9): holds one reference from
///                 keepResult() until the next beginOperation(), or forever
///                 if the Context is destroyed first — the destructor
///                 releases it explicitly.
///   hosts_        HostTable — the functions the host registered
///                 (host.hpp). Append-only; every release runs from
///                 ~HostTable, that is, when this Context is destroyed.
///   compiled_     true from the first compileExpression/compileScript on
///                 this Context until it is destroyed. Registration after
///                 that point is refused (docs/backlog.md B23).
///   evaluating_   true for the duration of one eval() or run(), including
///                 the host callbacks they invoke. Every door of the C API
///                 refuses while it is up.
class Context {
   public:
    Context() = default;

    /// Releases lastResult_ (Р9): without this, a Context destroyed while its
    /// result slot holds a reference leaks the box it points at — the same
    /// leak beginOperation prevents on every ordinary boundary, except there
    /// is no next boundary to drain it at.
    ~Context() { detail::releaseValue(lastResult_); }

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

    /// Keeps a value alive until the next operation boundary and returns a
    /// reference to the STORED copy — the one whose address the caller may
    /// borrow bytes out of.
    ///
    /// Returns a reference, not a value: bytes borrowed from a returned copy
    /// would die with that copy at the end of the caller's full expression.
    const Value &keepResult(Value v) {
        assert(lastResult_.kind() == Value::Kind::Null &&
               "the boundary must have cleared the previous result");
        detail::retainValue(v);
        lastResult_ = v;
        return lastResult_;
    }

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
        compiled_ = true;
        return Expression::compile(source, store_, out, diags, capacity);
    }

    /// Same door, for a Script.
    [[nodiscard]] std::uint32_t compileScript(std::string_view source, Script *out,
                                              Diagnostic *diags,
                                              std::uint32_t capacity) {
        compiled_ = true;
        return Script::compile(source, store_, out, diags, capacity);
    }

    /// Registers a host function. Refused after the first compilation on
    /// this Context, and refused from inside a callback.
    RegisterOutcome registerFunction(const ChupaFunction &desc) {
        if (evaluating_) { return RegisterOutcome::Reentrant; }
        if (compiled_)   { return RegisterOutcome::TooLate; }
        return hosts_.add(desc);
    }

    /// The functions registered here. Read by resolveCallee (callee.hpp)
    /// during compilation and by evaluation to reach the callback.
    [[nodiscard]] const HostTable &hosts() const noexcept { return hosts_; }

    /// Is a call in flight. The C API asks before doing anything on this
    /// Context: a host callback runs in the middle of a tree walk, and a
    /// write there would drain the deferred list the walk is standing on.
    [[nodiscard]] bool isEvaluating() const noexcept { return evaluating_; }

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
        detail::releaseValue(lastResult_);
        lastResult_ = Value::null();
    }

    Store store_;
    HostTable hosts_;
    bool compiled_ = false;
    bool evaluating_ = false;
    Execution exec_{store_};

    /// The value the last string shortcut borrowed its bytes from.
    ///
    /// A ROOT: it holds one reference, taken in keepResult and dropped at the
    /// next operation boundary. It exists for chupa_eval_string, which hands
    /// the host a pointer to the result's bytes: for a short string those
    /// bytes live inside the value itself, so a local variable inside
    /// c_api.cpp would die on return and take them with it. The header's
    /// promise — "the bytes stay valid until the next call that touches this
    /// context" — is met here literally, and now for one reason instead of
    /// three.
    Value lastResult_ = Value::null();
};

}  // namespace CS
