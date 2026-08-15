#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "store.hpp"
#include "value.hpp"

namespace CS {

/// Скомпилированное выражение: исходник и дерево, связанные навсегда.
///
/// Единица владеет копией своего исходника, поэтому правил времени жизни у
/// неё нет ни одного: буфер, из которого её собрали, может умереть сразу.
/// Владеет единицей тот, кто её создал (docs/backlog.md B35).
///
/// Ast наружу не отдаётся ни ссылкой, ни указателем: он приватная деталь.
/// Разлучить исходник и дерево снаружи нечем — обоих по отдельности снаружи
/// не существует.
class Expression {
   public:
    /// Пустая единица: ни исходника, ни дерева. Годна только под compile.
    /// Вычислять её нельзя: eval утверждается на отметке прохода, как это
    /// делает и evalExpression (core/src/eval.hpp).
    Expression() = default;

    /// Единственная дверь: разбор, проверка и связывание с исходником — разом.
    ///
    /// Возвращает число найденных ошибок; 0 — успех. Форма совпадает с
    /// compileExpression (core/src/compile.hpp): ошибка разбора даёт ровно
    /// единицу, ошибок проверки может быть сколько угодно, и в diags попадает
    /// не больше capacity первых.
    ///
    /// При отказе *out остаётся тем, чем был: неудачная компиляция не портит
    /// уже собранную единицу.
    ///
    /// Из store читается только состав имён (check.hpp); значения роли не
    /// играют, и удерживать store единица не будет.
    static std::uint32_t compile(std::string_view source, const Store &store,
                                 Expression *out, Diagnostic *diags,
                                 std::uint32_t capacity);

    /// Вычисляет выражение. При отказе возвращает false и заполняет diag;
    /// смещение считается от начала source(). При отказе *out не трогается.
    bool eval(Store &store, Value *out, Diagnostic &diag) const;

    [[nodiscard]] std::string_view source() const noexcept { return source_; }

   private:
    std::string source_;
    Ast ast_;
};

}  // namespace CS
