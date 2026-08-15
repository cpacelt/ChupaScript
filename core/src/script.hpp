#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "store.hpp"

namespace CS {

/// Скомпилированный скрипт: исходник и дерево, связанные навсегда.
///
/// Единица владеет копией своего исходника, поэтому правил времени жизни у
/// неё нет ни одного: буфер, из которого её собрали, может умереть сразу.
/// Владеет единицей тот, кто её создал (docs/backlog.md B35).
///
/// Ast наружу не отдаётся ни ссылкой, ни указателем: он приватная деталь.
/// Разлучить исходник и дерево снаружи нечем — обоих по отдельности снаружи
/// не существует.
class Script {
   public:
    /// Пустая единица: ни исходника, ни дерева. Годна только под compile.
    /// Запускать её нельзя: run утверждается на отметке прохода, как это
    /// делает и runScript (core/src/eval.hpp).
    Script() = default;

    /// Единственная дверь: разбор, проверка и связывание с исходником — разом.
    ///
    /// Возвращает число найденных ошибок; 0 — успех. Форма совпадает с
    /// compileScript (core/src/compile.hpp): ошибка разбора даёт ровно
    /// единицу, ошибок проверки может быть сколько угодно, и в diags попадает
    /// не больше capacity первых.
    ///
    /// При отказе *out остаётся тем, чем был: неудачная компиляция не портит
    /// уже собранную единицу.
    ///
    /// Из store читается только состав имён (check.hpp); значения роли не
    /// играют, и удерживать store единица не будет.
    static std::uint32_t compile(std::string_view source, const Store &store,
                                 Script *out, Diagnostic *diags,
                                 std::uint32_t capacity);

    /// Выполняет скрипт. У скрипта нет значения (docs/semantics.md §3.1);
    /// при отказе возвращает false и заполняет diag; смещение считается от
    /// начала source().
    bool run(Store &store, Diagnostic &diag) const;

    [[nodiscard]] std::string_view source() const noexcept { return source_; }

   private:
    std::string source_;
    Ast ast_;
};

}  // namespace CS
