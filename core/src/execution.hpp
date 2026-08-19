#pragma once
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

#include "aggregate.hpp"
#include "deferred.hpp"
#include "diagnostic.hpp"
#include "store.hpp"
#include "value.hpp"

namespace CS {

/// Одно выполнение: его временный регион и постоянное хранилище, над которым
/// оно идёт.
///
/// Вынесено из единицы, а не сложено в неё, чтобы одну скомпилированную
/// единицу можно было разделять между вьюшками ([B4], [B28]): изменяемое
/// состояние внутри неё запрещало бы и разделение, и вложенное выполнение
/// той же единицы. Владеет им вызывающий — сегодня контекст C API. Ячейки
/// локальных и начало активационной записи придут с объявлениями
/// (docs/backlog.md [B57]).
///
/// Живёт дольше одного вычисления намеренно: временный регион сбрасывается с
/// сохранением ёмкости, и в установившемся режиме обращений к аллокатору не
/// остаётся. Экземпляр на вызов эту ёмкость терял бы каждый раз.
///
/// Постоянное хранилище держится ссылкой, а не передаётся рядом параметром.
/// Сначала было именно рядом, и это оказалось неверно дважды: во-первых, ни
/// одна функция вычислителя не берёт одно без другого, во-вторых, две половины
/// можно было передать несовпадающими — и молча прочитать индекс не из того
/// пула. Здесь такой пары не существует.
///
/// Не копируется и не перемещается: ссылка связывает экземпляр с конкретным
/// хранилищем на всю жизнь.
class Execution {
   public:
    /// Арена не берёт у постоянного хранилища ничего: общего у них не
    /// осталось ни одного члена.
    explicit Execution(Store &persistent) noexcept
        : scratch(Store::Role::Arena), persistent_(persistent) {}

    Execution(const Execution &) = delete;
    Execution &operator=(const Execution &) = delete;
    Execution(Execution &&) = delete;
    Execution &operator=(Execution &&) = delete;

    /// Временный регион: байтовая арена операции. Агрегатов в нём не бывает —
    /// они коробки, и живут по счётчику, а не по региону.
    Store scratch;

    /// Постоянный регион: таблица глобальных переменных и оснастка. Пишется
    /// только через promote — иначе туда попала бы строка из арены операции.
    [[nodiscard]] Store &persistent() noexcept { return persistent_; }
    [[nodiscard]] const Store &persistent() const noexcept { return persistent_; }

    /// Байты строки.
    ///
    /// Хранилище в вопросе больше не участвует. Коробка самодостаточна и
    /// читается без всякого пула, а промежуточная строка — смещение, и арена,
    /// придающая ему смысл, во всём выполнении ровно одна. Спрашивать «в каком
    /// хранилище лежит это значение» стало не у кого: раньше на этот вопрос
    /// отвечал storeOf, и у агрегата ответ был предрешён.
    ///
    /// Срез действителен до ближайшей записи в арену либо до смерти коробки.
    [[nodiscard]] std::string_view string(Value v) const noexcept {
        return scratch.string(v);
    }

    /// Принять значение туда, что переживёт текущую операцию: промежуточная
    /// строка становится коробкой, прочее проходит как есть.
    ///
    /// Звать надо перед укладкой в агрегат либо в глобальную переменную.
    /// Агрегат арены — такая же коробка и умеет уехать, продвигать его незачем.
    ///
    /// Живёт здесь, а не у хранилища, потому что берёт по одному от каждой
    /// половины выполнения: смещение читает та арена, что его выдала, а ссылку
    /// на новую коробку принимает список этого же выполнения.
    [[nodiscard]] Value promote(Value v) {
        if (detail::materialized(v)) { return v; }
        return CS::materialize(scratch.string(v), deferred_);
    }

    /// Список отложенного освобождения этого выполнения — один на оба
    /// хранилища (см. deferred.hpp).
    ///
    /// Прямой член, а не указатель в хранилище. Пока список лежал там, арена
    /// держала на него указатель, и всякое обращение стоило разыменования на
    /// пути, по которому ходит каждое присваивание.
    [[nodiscard]] Deferred &deferred() noexcept { return deferred_; }

    /// Таблица имён полей контекста — та единственная, что есть. Нужна тому,
    /// кто создаёт объект (CS::makeObject); владеет ею постоянное хранилище.
    [[nodiscard]] KeyTable *keys() const noexcept { return persistent_.keys(); }

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
        if (unitStoreId == persistent_.id()) { return true; }
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

   private:
    /// Scratch buffer for format. Declared before deferred_ only for
    /// readability; it owns nothing that ordering could affect.
    std::string builder_;

    /// Объявлен после арены и до ссылки на хранилище: разрушается раньше
    /// обоих, а значит отпускает всё, что накопил, пока живы и та и другое.
    Deferred deferred_;
    Store &persistent_;
};

}  // namespace CS
