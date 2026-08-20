#include "check.hpp"

#include <string_view>

#include "builtin.hpp"
#include "callee.hpp"

namespace CS {
namespace {

/// Состояние одного прохода: копит находки, не останавливаясь.
struct Checker {
    Ast &ast;
    std::string_view source;
    const Store &store;
    Diagnostic *out;
    std::uint32_t capacity;
    const HostTable *hosts;
    CompileMode mode;
    std::uint32_t found = 0;

    void report(NodeId node, ErrorCode code, const char *message) {
        if (found < capacity && out != nullptr) {
            out[found] = Diagnostic{code, ast.offset(node), message};
        }
        ++found;
    }

    /// Вызов, чей результат употреблён, обязан возвращать значение (§6.2).
    ///
    /// Имя берётся разрешённым: пост-обход гарантирует, что checkCall прошёл по
    /// этому узлу раньше, чем родитель добрался до него отсюда. Нет разрешения
    /// — имя неизвестно, и об этом уже сообщено.
    void requireValue(NodeId call) {
        if (!ast.hasCallee(call)) { return; }  // уже сообщено
        if (!calleeOf(hosts, ast.callee(call)).returnsValue) {
            report(call, ErrorCode::Name, "builtin does not return a value");
        }
    }

    /// Вызов в позиции стейтмента обязан значения не возвращать (§6.1).
    void requireVoid(NodeId call) {
        if (!ast.hasCallee(call)) { return; }
        if (calleeOf(hosts, ast.callee(call)).returnsValue) {
            report(call, ErrorCode::Name, "call result is not used");
        }
    }

    void checkCall(NodeId node) {
        const Callee callee = resolveCallee(hosts, ast.text(node, source));
        if (callee.ref == kNoCallee) {
            report(node, ErrorCode::Name, "unknown function");
            return;
        }
        // Кладётся до проверки арности намеренно: неверное число аргументов —
        // ошибка, до вычисления такое дерево не доходит, а разрешение всё
        // равно верное, и хранить его половинчато не за что.
        ast.setCallee(node, callee.ref);

        const std::uint32_t count = ast.childCount(node);
        if (count < callee.minArgs ||
            (callee.maxArgs != kVariadic && count > callee.maxArgs)) {
            report(node, ErrorCode::Name, "wrong number of arguments");
            return;
        }

        // Грязный вызов в выражении. Спрашивается только у хост-функций: у
        // билтинов тот же факт уже закрыт правилом §6.2 «результат Void
        // употреблять нельзя», и это правило и есть доказательство §6.3.
        // Вторая жалоба на один факт удвоила бы вывод компилятора, а первым
        // сообщением осталось бы менее точное.
        if (isHostCallee(callee.ref) && !callee.pure &&
            mode == CompileMode::Expression) {
            report(node, ErrorCode::Usage,
                   "impure function cannot be called from an expression");
            return;
        }

        if (isHostCallee(callee.ref) ||
            builtinOfCallee(callee.ref) != Builtin::Format) {
            return;
        }

        // Шаблон-литерал сверяется здесь; иначе проверка уходит в выполнение
        // (docs/semantics.md §8.8). Считаем по сырому, недекодированному
        // тексту — это верно и для escape-последовательностей: лексер
        // (core/src/lexer.cpp) декодирует только \\ \' \" \n \t, ни одна не
        // даёт байт $, { или }, и каждая разворачивается из двух символов
        // ровно в один — границы ${} декодированием не создаются и не
        // разрушаются. Если в лексер добавят escape, дающий $, { или } —
        // сюда придётся вернуться: подсчёт по сырому тексту перестанет
        // совпадать с подсчётом по декодированному.
        const NodeId tmpl = ast.child(node, 0);
        if (ast.kind(tmpl) != NodeKind::String) { return; }
        if (countPlaceholders(ast.text(tmpl, source)) != count - 1) {
            report(node, ErrorCode::Name,
                   "format placeholder count does not match arguments");
        }
    }

    void checkNode(NodeId node) {
        switch (ast.kind(node)) {
            case NodeKind::Call:
                checkCall(node);
                break;

            case NodeKind::Identifier: {
                // Узлы Identifier — это в точности обращения к именам: имя поля
                // у Member лежит текстом, а не ребёнком.
                //
                // Здесь же имя разрешается в номер ячейки и кладётся в узел:
                // проход и так ищет имя, чтобы отвергнуть незнакомое, и второй
                // раз искать его на каждом вычислении незачем. Это
                // единственное место, где имя глобальной переменной вообще
                // ищется на пути чтения.
                const GlobalSlot slot = store.globalSlot(ast.text(node, source));
                if (slot == kNoGlobalSlot) {
                    report(node, ErrorCode::Name, "unknown name");
                } else {
                    ast.setGlobalValuesSlot(node, slot);
                }
                break;
            }

            case NodeKind::Assign:
                // Целью не может быть само имя (docs/semantics.md §7.2).
                if (ast.kind(ast.child(node, 0)) == NodeKind::Identifier) {
                    report(ast.child(node, 0), ErrorCode::Name,
                           "cannot assign to a variable name");
                }
                break;

            default:
                break;
        }

        // Употреблён ли результат вызова, видно от родителя: Call среди детей
        // означает, что его значение куда-то идёт. Родитель у узла один, а
        // пост-обход гарантирует, что дети идут раньше.
        const std::uint32_t children = ast.childCount(node);
        for (std::uint32_t i = 0; i < children; ++i) {
            const NodeId child = ast.child(node, i);
            if (ast.kind(child) != NodeKind::Call) { continue; }
            if (ast.kind(node) == NodeKind::CallStatement) {
                requireVoid(child);
            } else {
                requireValue(child);
            }
        }
    }
};

}  // namespace

std::uint32_t check(Ast &ast, std::string_view source, const Store &store,
                    Diagnostic *out, std::uint32_t capacity,
                    const HostTable *hosts, CompileMode mode) {
    const NodeId root = ast.root();
    if (root == kNoNode) { return 0; }

    Checker checker{ast, source, store, out, capacity, hosts, mode};
    // Плоский цикл, а не рекурсия: узлы лежат в пост-обходе, дети раньше
    // родителей, и проверкам пропускать нечего — значит предела глубины у
    // прохода нет вовсе.
    for (NodeId node = 1; node <= root; ++node) {
        checker.checkNode(node);
    }
    // У корня родителя нет. Если корень — вызов, его результат и есть значение
    // выражения, то есть употреблён.
    if (ast.kind(root) == NodeKind::Call) { checker.requireValue(root); }

    if (checker.found == 0) { ast.markChecked(); }
    return checker.found;
}

}  // namespace CS
