#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "execution.hpp"
#include "store.hpp"

namespace CS {

class HostTable;

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
    /// Из store читается только состав имён (check.hpp): значения роли не
    /// играют. Пишется в него ровно одно — байты строковых литералов, разово
    /// (compile.hpp). Ссылки на store единица не удерживает, но с этого
    /// момента годна только для него: и номера ячеек, и уложенные литералы
    /// адресуют его пулы.
    ///
    /// hosts defaults to nullptr — see compileScript (compile.hpp) for what
    /// that means.
    static std::uint32_t compile(std::string_view source, Store &store,
                                 Script *out, Diagnostic *diags,
                                 std::uint32_t capacity,
                                 const HostTable *hosts = nullptr);

    /// Выполняет скрипт. У скрипта нет значения (docs/semantics.md §3.1);
    /// при отказе возвращает false и заполняет diag; смещение считается от
    /// начала source().
    ///
    /// Примитив, как и Expression::eval: граница операции здесь не проходит,
    /// встраиваться надо через Context::run (core/src/context.hpp).
    bool run(Execution &exec, Diagnostic &diag) const;

    /// Срез живёт, пока жива *эта* единица и не менялась перекомпиляцией:
    /// после разрушения объекта либо после следующего compile() срез
    /// провисает — обычное для string_view правило, но раз выше объявлено
    /// отсутствие правил времени жизни у самой единицы, здесь оно
    /// уточняется отдельно.
    [[nodiscard]] std::string_view source() const noexcept { return source_; }

   private:
    std::string source_;
    Ast ast_;

    /// Id of the Store this unit was compiled against; 0 until compile()
    /// succeeds. run() compares it against the Store the Execution runs over
    /// and refuses a mismatch — see Store::id().
    std::uint32_t storeId_ = 0;
};

}  // namespace CS
