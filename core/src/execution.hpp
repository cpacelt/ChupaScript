#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "aggregate.hpp"
#include "deferred.hpp"
#include "diagnostic.hpp"
#include "store.hpp"
#include "value.hpp"

/// The C boundary's context handle (defined in c_api.cpp). Opaque here on
/// purpose: the core carries it for a host callback but never looks inside
/// it, so a forward declaration is all this header needs.
struct ChupaContext;

namespace CS {

class HostTable;

/// One evaluation: its string builder and deferred list, plus the Store it
/// runs over.
///
///   Execution::builder_    a plain std::string, scratch space for format
///   Execution::deferred_   creator references to release at the next
///                          operation boundary (see deferred.hpp)
///   Execution::store_      &Store — the global variables this evaluation
///                          reads and writes; owned by the caller, not by
///                          this Execution
///   Execution::hosts_      const HostTable * — the functions the host
///                          registered, read to reach a callback; nullptr
///                          means none are registered, owned by the Context
///   Execution::hostHandle_ ChupaContext * — passed to a callback as its
///                          first argument and never dereferenced here.
///                          Opaque on purpose: this is the C boundary's
///                          type, and the core has no business inside it.
///   Execution::argStack_   Value storage shared by every call in flight.
///                          One call owns the half-open range
///                          [base, base + count); an ArgFrame keeps that
///                          range and gives it back on destruction.
///   Execution::hostFailureCode_  ErrorCode — set by Context::setHostFailure
///                          (a forward from chupa_fail), read and reset by
///                          takeHostFailure. None means the callback refused
///                          without calling chupa_fail.
///   Execution::hostFailureText_  std::string — an OWNING copy of the reason
///                          a host callback gave for refusing; the host's own
///                          buffer is gone by the time anyone reads this.
///
/// A String, Object or Array Value is a box (box.hpp) and needs no Store to
/// be read — CS::stringBytes and the free functions in aggregate.hpp read a
/// box directly. What the Store still supplies here is the field-name table
/// (keys()) that a new object interns its keys into, and the global variable
/// table that identifier lookups and assignments read and write.
///
/// Вынесено из единицы, а не сложено в неё, чтобы одну скомпилированную
/// единицу можно было разделять между вьюшками ([B4], [B28]): изменяемое
/// состояние внутри неё запрещало бы и разделение, и вложенное выполнение
/// той же единицы. Владеет им вызывающий — сегодня контекст C API. Ячейки
/// локальных и начало активационной записи придут с объявлениями
/// (docs/backlog.md [B57]).
///
/// Живёт дольше одного вычисления намеренно: builder_ сбрасывается с
/// сохранением ёмкости, и в установившемся режиме обращений к аллокатору не
/// остаётся. Экземпляр на вызов эту ёмкость терял бы каждый раз.
///
/// Store держится ссылкой, а не передаётся рядом параметром.
/// Сначала было именно рядом, и это оказалось неверно дважды: во-первых, ни
/// одна функция вычислителя не берёт одно без другого, во-вторых, две половины
/// можно было передать несовпадающими — и молча прочитать индекс не из того
/// пула. Здесь такой пары не существует.
///
/// Не копируется и не перемещается: ссылка связывает экземпляр с конкретным
/// хранилищем на всю жизнь.
class Execution {
   public:
    explicit Execution(Store &store) noexcept : store_(store) {}

    /// Wires the table in at construction, not through a setter that a
    /// caller could forget to call: a setter was tried first and dropped —
    /// nothing forced it to run, so a real Context could build an Execution
    /// whose hosts_ silently stayed nullptr forever.
    Execution(Store &store, const HostTable *hosts) noexcept
        : store_(store), hosts_(hosts) {}

    Execution(const Execution &) = delete;
    Execution &operator=(const Execution &) = delete;
    Execution(Execution &&) = delete;
    Execution &operator=(Execution &&) = delete;

    /// Хранилище глобальных переменных этого выполнения.
    [[nodiscard]] Store &store() noexcept { return store_; }
    [[nodiscard]] const Store &store() const noexcept { return store_; }

    /// Список отложенного освобождения этого выполнения (см. deferred.hpp).
    [[nodiscard]] Deferred &deferred() noexcept { return deferred_; }

    /// The host functions this evaluation may call, or nullptr for none.
    [[nodiscard]] const HostTable *hosts() const noexcept { return hosts_; }

    /// The opaque handle a host callback receives as its first argument.
    [[nodiscard]] ChupaContext *hostHandle() const noexcept {
        return hostHandle_;
    }

    /// Set once, by chupa_context_create — see the LAYOUT note on
    /// hostHandle_ above for why the core never looks inside it.
    void setHostHandle(ChupaContext *handle) noexcept { hostHandle_ = handle; }

    /// Таблица имён полей контекста — та единственная, что есть. Нужна тому,
    /// кто создаёт объект (CS::makeObject); владеет ею хранилище.
    [[nodiscard]] KeyTable *keys() const noexcept { return store_.keys(); }

    /// Rejects a compiled unit that belongs to another Store.
    ///
    /// The check is unconditional, not an assert: a release build is exactly
    /// where the foreign slot number would be read silently, and a
    /// neighbouring variable's value arriving on screen is the failure this
    /// closes.
    ///
    /// Lives here rather than beside each entry point because the Execution is
    /// what knows which Store it runs over; a free helper would have to be
    /// duplicated into every translation unit that evaluates.
    [[nodiscard]] bool acceptsUnit(std::uint32_t unitStoreId,
                                   Diagnostic &diag) const noexcept {
        if (unitStoreId == store_.id()) { return true; }
        diag = Diagnostic{ErrorCode::Usage, 0,
                          "unit was compiled against another context"};
        return false;
    }

    // ─── string builder ───
    //
    //   Execution::builder_    a plain std::string, NOT a region
    //   ┌──────────────────────────────────────────┐
    //   │ ...outer format's pieces... │ inner's... │
    //   └──────────────────────────▲──────────────▲┘
    //                        outer mark      inner mark
    //
    // format (docs/semantics.md §8.8) needs a growing buffer because the
    // length of its result is not known before the last piece is appended.
    //
    // The buffer is NOT a memory region: no Value ever points into it, it
    // lives from beginString to the matching endString, and clearing it is a
    // detail of one function rather than a rule of the memory model. What it
    // keeps from the arena it replaces is the capacity: endString truncates
    // the buffer back to the mark and leaves the allocation, so a steady
    // stream of format calls stops reaching the allocator.
    //
    // A mark is a POSITION, not a pointer: appending may move the buffer's
    // bytes, and a pointer taken before the move would address freed memory.
    // Nested format works for the same reason — the inner call's mark sits
    // above the outer call's, and endString lifts only the tail above it.

    /// Starts building a string. The returned mark goes to endString or
    /// abortString; every path out of a build must pass through one of them.
    [[nodiscard]] std::uint32_t beginString() noexcept {
        // A builder that large is a different problem, and it trips a debug
        // assertion — compiled out under NDEBUG.
        assert(builder_.size() <= 0xffffffffu && "string builder outgrew uint32");
        return static_cast<std::uint32_t>(builder_.size());
    }

    void appendToString(std::string_view bytes) { builder_.append(bytes); }

    /// Finishes the build: copies everything appended above the mark into a
    /// box and truncates the buffer back to the mark.
    ///
    /// The result is a box rather than an offset into a region, and that is
    /// the whole change: a box is self-contained, so the caller may store it
    /// in a global variable, put it into an aggregate or hand it to the host
    /// without asking anyone's permission first. The creator's reference goes
    /// to this Execution's deferred list, so a result nobody keeps dies at the
    /// next operation boundary instead of leaking.
    [[nodiscard]] Value endString(std::uint32_t mark) {
        const Value result =
            CS::materialize(std::string_view(builder_).substr(mark), deferred_);
        builder_.resize(mark);
        return result;
    }

    /// Abandons the build, truncating the buffer back to the mark.
    void abortString(std::uint32_t mark) noexcept { builder_.resize(mark); }

    /// Stashes the reason a host callback gave for its refusal — the forward
    /// target of Context::setHostFailure (context.hpp). text is copied
    /// immediately: chupa_fail's own caller may free its buffer the instant
    /// the call returns.
    void setHostFailure(ErrorCode code, std::string_view text) {
        hostFailureCode_ = code;
        hostFailureText_.assign(text);
    }

    /// Reads back what setHostFailure stashed and resets the code, so the
    /// reason for one refusal is never mistaken for the reason of the next
    /// one: code is ErrorCode::None when the callback refused without ever
    /// calling chupa_fail — failHostCall (eval.cpp) is what turns that into
    /// ErrorCode::Host and a fixed message, and it branches on code alone,
    /// never on hostFailureText_.
    ///
    /// hostFailureText_ itself is left as is, not cleared: the returned
    /// message points straight into its buffer, and a std::string::clear()
    /// here would overwrite that buffer's first byte with a NUL — its own
    /// terminator — the instant before the caller reads through the pointer
    /// this call just handed back. The text lives on inertly until the next
    /// setHostFailure overwrites it, which is exactly the "valid until the
    /// next call on ctx" rule the public header promises for chupa_fail's
    /// message.
    [[nodiscard]] Diagnostic takeHostFailure() noexcept {
        Diagnostic out{hostFailureCode_, 0, hostFailureText_.c_str()};
        hostFailureCode_ = ErrorCode::None;
        return out;
    }

   private:
    friend class ArgFrame;

    /// Scratch buffer for format. Declared before deferred_ only for
    /// readability; it owns nothing that ordering could affect.
    std::string builder_;

    /// Объявлен после сборщика и до ссылки на хранилище: разрушается раньше
    /// обоих, а значит отпускает всё, что накопил, пока живы и та и другое.
    Deferred deferred_;
    Store &store_;

    /// A pointer, not a reference like store_: a reference would have to be
    /// bound at construction, which is exactly the 169-call-site cost the
    /// controller's note above rejects. nullptr for "none registered" is the
    /// same convention compilation already uses for the same reason.
    const HostTable *hosts_ = nullptr;
    ChupaContext    *hostHandle_ = nullptr;
    std::vector<Value> argStack_;
    ErrorCode   hostFailureCode_ = ErrorCode::None;
    std::string hostFailureText_;
};

/// One call's arguments, living on the Execution's shared stack.
///
/// LAYOUT — the frame owns a half-open range and nothing else:
///
///   exec_   &Execution — whose stack this range is cut from
///   base_   index of the first slot
///   count_  how many slots
///
/// Lifetime: the range is valid from construction to destruction of this
/// frame. Frames nest and unwind in order, because an argument of an outer
/// call may itself be a call.
class ArgFrame {
   public:
    ArgFrame(Execution &exec, std::uint32_t count)
        : exec_(exec), base_(exec.argStack_.size()), count_(count) {
        // Value has no public default constructor (value.hpp keeps its
        // factories closed), so resize needs the fill argument.
        exec_.argStack_.resize(base_ + count, Value::null());
    }

    ~ArgFrame() { exec_.argStack_.resize(base_, Value::null()); }

    ArgFrame(const ArgFrame &) = delete;
    ArgFrame &operator=(const ArgFrame &) = delete;
    ArgFrame(ArgFrame &&) = delete;
    ArgFrame &operator=(ArgFrame &&) = delete;

    Value &operator[](std::uint32_t i) noexcept {
        assert(i < count_);
        return exec_.argStack_[base_ + i];
    }

    /// The arguments as a contiguous block.
    ///
    /// Recomputed on every call and NEVER cached by the caller: a nested
    /// frame may have grown the vector and moved its buffer since the last
    /// time this was taken. Taking this pointer is safe exactly once — after
    /// every argument of this call has been evaluated, when no nested frame
    /// is left alive and nothing can push again, because a host callback
    /// runs on a closed frame: every C API door that could push a new frame
    /// refuses while a call is in flight (refuseWhileEvaluating, c_api.cpp),
    /// in release builds as much as in debug ones.
    [[nodiscard]] const Value *data() const noexcept {
        return exec_.argStack_.data() + base_;
    }

    [[nodiscard]] std::uint32_t size() const noexcept { return count_; }

   private:
    Execution     &exec_;
    std::size_t    base_;
    std::uint32_t  count_;
};

}  // namespace CS
