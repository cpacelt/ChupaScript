#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "execution.hpp"
#include "store.hpp"
#include "value.hpp"

namespace CS {

/// Исход типизированного вычисления.
///
/// Трёхзначен по необходимости: «получилось null» физически некуда положить,
/// когда выходной параметр — double *. У сырого eval такой нужды нет, там
/// null — обычное значение, и он возвращает bool.
enum class EvalStatus : std::uint8_t { Ok, Null, Error };

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
    /// Из store читается только состав имён (check.hpp): значения роли не
    /// играют. Ссылки на store единица не удерживает, но с этого момента
    /// годна только для него: номера ячеек адресуют его пулы.
    static std::uint32_t compile(std::string_view source, Store &store,
                                 Expression *out, Diagnostic *diags,
                                 std::uint32_t capacity);

    /// Вычисляет выражение. При отказе возвращает false и заполняет diag;
    /// смещение считается от начала source(). При отказе *out не трогается.
    ///
    /// Примитив: хранилище и состояние выполнения передаются по отдельности,
    /// и граница операции здесь не проходит. Встраиваться надо через
    /// Context::eval (core/src/context.hpp) — он держит пару вместе и владеет
    /// границей. Здесь — потому что на этом методе живёт описание исходов и
    /// его зовут тесты.
    bool eval(Execution &exec, Value *out, Diagnostic &diag) const;

    /// Срез живёт, пока жива *эта* единица и не менялась перекомпиляцией:
    /// после разрушения объекта либо после следующего compile() срез
    /// провисает — обычное для string_view правило, но раз выше объявлено
    /// отсутствие правил времени жизни у самой единицы, здесь оно
    /// уточняется отдельно.
    [[nodiscard]] std::string_view source() const noexcept { return source_; }

    /// Вычисляет и достаёт результат нужного типа.
    ///
    /// Ok — значение положено в *out. Null — выражение дало null, *out не
    /// тронут. Error — ошибка вычисления либо несовпадение типа, подробности
    /// в diag, *out не тронут. На исходах Ok и Null diag не трогается вовсе:
    /// он остаётся тем, чем был у вызывающего до вызова, — включая
    /// устаревшую ошибку от прошлого раза, если вызывающий её не сбросил.
    EvalStatus evalNumber(Execution &exec, double *out,
                          Diagnostic &diag) const;
    EvalStatus evalBool  (Execution &exec, bool *out, Diagnostic &diag) const;

   private:
    /// Вычисляет и проверяет вид значения. Ok — значение нужного вида лежит
    /// в *out и остаётся только достать его. Остальные исходы — как у
    /// публичных методов.
    EvalStatus evalOfKind(Execution &exec, Value::Kind wanted,
                          const char *message, Value *out,
                          Diagnostic &diag) const;

    std::string source_;
    Ast ast_;

    /// Id of the Store this unit was compiled against; 0 until compile()
    /// succeeds. Every eval entry point compares it against the Store the
    /// Execution runs over and refuses a mismatch — see Store::id().
    std::uint32_t storeId_ = 0;
};

}  // namespace CS
