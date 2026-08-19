#include "compile.hpp"

#include <string>
#include <string_view>

#include "check.hpp"
#include "parser.hpp"
#include "text.hpp"

namespace CS {
namespace {

std::uint32_t reportParseFailure(const Diagnostic &diag, Diagnostic *out,
                                 std::uint32_t capacity) {
    if (capacity > 0 && out != nullptr) { out[0] = diag; }
    return 1;
}

/// Укладывает байты каждого строкового литерала в пул текста хранилища и кладёт
/// получившееся значение в узел.
///
/// Литерал неизменен, поэтому одной копии на всю жизнь единицы довольно, и
/// вычисление сводится к чтению готового Value из узла. Раньше копия делалась
/// заново на каждом вычислении, а пул текста поштучно не освобождается — то
/// есть выражение со строкой, пересчитываемое на каждый кадр, растило память
/// монотонно (docs/backlog.md B51).
///
/// Экранирование раскодируется здесь же, поэтому черновик scratch заводится
/// один на весь проход, а не на каждое вычисление.
///
/// Зовётся только после успешной проверки: на дереве с ошибками писать в
/// хранилище незачем, а мусор в пуле остался бы навсегда.
void internStringLiterals(Ast &ast, std::string_view source, Store &store) {
    const NodeId root = ast.root();
    std::string scratch;
    // Тот же плоский цикл, что и в check: узлы лежат в пост-обходе, и обойти
    // надо все — литерал бывает где угодно, вплоть до ключа объекта.
    for (NodeId node = 1; node <= root; ++node) {
        if (ast.kind(node) != NodeKind::String) { continue; }
        // Узел дерева хранит указатель, а не готовый Value: шестнадцать байт в
        // него не влезли бы, не растя сам узел, а восемь ложатся туда, где у
        // узла без детей всё равно пустота.
        ast.setStringLiteral(node,
                             store.internLiteral(literalText(ast, node, source, scratch)));
    }
}

}  // namespace

std::uint32_t compileExpression(const char *source, std::uint32_t length,
                                Ast &ast, Store &store, Diagnostic *out,
                                std::uint32_t capacity) {
    Diagnostic diag;
    if (!parseExpression(source, length, ast, diag)) {
        return reportParseFailure(diag, out, capacity);
    }
    const std::string_view text(source, length);
    const std::uint32_t errors = check(ast, text, store, out, capacity);
    if (errors == 0) { internStringLiterals(ast, text, store); }
    return errors;
}

std::uint32_t compileScript(const char *source, std::uint32_t length, Ast &ast,
                            Store &store, Diagnostic *out,
                            std::uint32_t capacity) {
    Diagnostic diag;
    if (!parseScript(source, length, ast, diag)) {
        return reportParseFailure(diag, out, capacity);
    }
    const std::string_view text(source, length);
    const std::uint32_t errors = check(ast, text, store, out, capacity);
    if (errors == 0) { internStringLiterals(ast, text, store); }
    return errors;
}

}  // namespace CS
