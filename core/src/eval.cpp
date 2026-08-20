#include "eval.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "builtin.hpp"
#include "box.hpp"
#include "operator.hpp"
#include "text.hpp"
#include "aggregate.hpp"

namespace CS {
namespace {

// Предварительное объявление для readKey
bool eval(const Ast &ast, std::string_view source, NodeId node, Execution &exec, Value *out, Diagnostic &diag);

/// Записывает отказ с местом узла.
///
/// Соглашение — первая ошибка выигрывает, и держится оно целиком на вызывающих:
/// проверки «уже отказали» здесь нет, повторный вызов диагностику перезапишет.
/// Поэтому всякий, кто получил false, обязан вернуть false немедленно, не
/// продолжая обход и не вызывая fail ещё раз. С приходом частей 2 и 3
/// вызывающих станет втрое больше, а правило останется тем же.
bool fail(const Ast &ast, NodeId node, ErrorCode code, const char *message,
          Diagnostic &diag) {
    diag = Diagnostic{code, ast.offset(node), message};
    return false;
}

/// Чтение ключа у базы (docs/semantics.md §6.2, §6.3, §6.4).
///
/// Объект — значение либо null; null — null; прочее — ошибка. Один и тот же
/// разбор обслуживает и obj.k, и obj[k]: отличаются они только тем, откуда
/// берётся ключ.
bool readKey(const Ast &ast, NodeId node, Value base, std::string_view key,
             Value *out, Diagnostic &diag) {
    switch (base.kind()) {
        case Value::Kind::Object:
            *out = CS::objectGet(base, key);
            return true;
        case Value::Kind::Null:
            *out = Value::null();
            return true;
        default:
            return fail(ast, node, ErrorCode::Type, "only objects have keys",
                        diag);
    }
}

/// Приведение скаляра к строке (docs/semantics.md §4).
///
/// Тонкая обёртка над builtin.hpp::coerceScalarToString, которая знает про
/// узел дерева ровно настолько, чтобы взять из него смещение для диагностики
/// — правило §4 записано один раз, в builtin.cpp, и обслуживает и ключ
/// объекта, и has, и str с format.
///
/// A string value reads through CS::stringBytes without consulting a Store:
/// a long string is a box (box.hpp) and the slice is valid until the box's
/// reference count reaches zero; a short string carries its bytes inside the
/// value itself, and the slice is valid exactly as long as that value is.
///
/// value is taken by const reference, not by value: an inline string's bytes
/// live inside the value itself, and a by-value copy here would die on
/// return while *out still pointed into it. The caller's own named Value
/// must outlive this call.
///
/// numberBuffer обязан быть размером не меньше kNumberBufferSize.
bool coerceToString(const Ast &ast, NodeId node, const Value &value,
                    char *numberBuffer, std::string_view *out,
                    Diagnostic &diag) {
    return coerceScalarToString(value, numberBuffer, out, ast.offset(node), diag);
}

/// Собирает строку по шаблону (docs/semantics.md §8.8).
///
/// Живёт здесь, а не в builtin.cpp, потому что format вариадичен: буфер под
/// заранее вычисленные аргументы потребовал бы верхней границы их числа,
/// которой §8.8 не устанавливает. Шаблон потребляет аргументы строго слева
/// направо, по одному на плейсхолдер, поэтому лениво выходит и проще, и без
/// придуманного предела.
///
/// The template is parsed by nextFormatPiece (builtin.hpp) — the same function
/// the static pass uses on a literal template (check.cpp), so the rule
/// "$${} is a literal, ${} is a placeholder" is written once and keeps both
/// readers in step by construction rather than by agreement.
///
/// The template is read where it lies and the result is assembled in
/// Execution's builder; the two are different buffers, and that is what makes
/// evaluating an argument in the middle of a build safe. tmpl is a value the
/// caller of evalFormat still holds a reference to for the whole loop below,
/// so nothing an argument does can move or free the bytes this loop is
/// reading — the concern that made the old code re-slice the template on
/// every iteration is gone with the arena.
bool evalFormat(const Ast &ast, std::string_view source, NodeId node,
                Execution &exec, Value *out, Diagnostic &diag) {
    const std::uint32_t argCount = ast.childCount(node);

    Value tmpl = Value::null();
    if (!eval(ast, source, ast.child(node, 0), exec, &tmpl, diag)) { return false; }
    if (tmpl.kind() != Value::Kind::String) {
        return fail(ast, node, ErrorCode::Type,
                    "format expects a string template", diag);
    }

    const std::uint32_t mark = exec.beginString();
    std::uint32_t next = 1;  // следующий аргумент
    FormatCursor cursor;

    for (;;) {
        FormatPiece kind = FormatPiece::Literal;
        std::string_view chunk;
        if (!nextFormatPiece(stringBytes(tmpl), cursor, &kind, &chunk)) { break; }

        if (kind == FormatPiece::Literal) {
            exec.appendToString(chunk);
            continue;
        }
        if (kind == FormatPiece::Escaped) {
            exec.appendToString("${}");
            continue;
        }

        // FormatPiece::Placeholder — потребляет следующий аргумент.
        if (next >= argCount) {
            exec.abortString(mark);
            return fail(ast, node, ErrorCode::Type,
                        "format placeholder count does not match arguments",
                        diag);
        }
        Value argument = Value::null();
        if (!eval(ast, source, ast.child(node, next), exec, &argument, diag)) {
            exec.abortString(mark);
            return false;
        }
        ++next;

        char buffer[kNumberBufferSize];
        std::string_view text;
        if (!coerceToString(ast, node, argument, buffer, &text, diag)) {
            exec.abortString(mark);
            return false;
        }
        exec.appendToString(text);
    }

    if (next != argCount) {
        exec.abortString(mark);
        return fail(ast, node, ErrorCode::Type,
                    "format placeholder count does not match arguments", diag);
    }
    *out = exec.endString(mark);
    return true;
}

/// Проверяет индекс массива по правилам §6.1: Number, конечный, целый, не
/// отрицательный. Решение про границу остаётся вызывающему — оно у чтения и
/// записи разное: чтение за границей штатно даёт null, запись — ошибка Range.
bool checkArrayIndex(const Ast &ast, NodeId node, Value subscript, double *out,
                     Diagnostic &diag) {
    if (subscript.kind() != Value::Kind::Number) {
        return fail(ast, node, ErrorCode::Type, "array index must be a number",
                    diag);
    }

    const double index = subscript.numberValue();
    // Дробный и отрицательный индекс — ошибка автора, а не неполнота данных:
    // приведения к целому в языке нет.
    if (!std::isfinite(index) || index < 0.0 || index != std::floor(index)) {
        return fail(ast, node, ErrorCode::Range,
                    "array index must be a non-negative integer", diag);
    }
    *out = index;
    return true;
}

/// Чтение элемента массива (docs/semantics.md §6.1).
bool readIndex(const Ast &ast, NodeId node, Value array, Value subscript,
               Value *out, Diagnostic &diag) {
    double index = 0.0;
    if (!checkArrayIndex(ast, node, subscript, &index, diag)) { return false; }

    // За границей — штатное чтение. Сравнение в double, потому что индекс
    // может превышать всё, что влезает в uint32.
    if (index >= static_cast<double>(CS::arrayCount(array))) {
        *out = Value::null();
        return true;
    }
    *out = CS::arrayAt(array, static_cast<std::uint32_t>(index));
    return true;
}

/// Обход дерева. Рекурсия, а не цикл: короткому замыканию нужен пропуск
/// поддеревьев, а циклу — буфер значений на всё дерево (спека §3).
///
/// Собственного предела глубины нет: её ограничил парсер — но не тем, что
/// ограничил свою рекурсию, а тем, что kMaxDepth считает высоту дерева целиком.
/// Конструкции, которые парсер разбирает циклом, глубины разбора не добавляют,
/// однако уровень дерева дают, и потому тратят ту же единицу бюджета, что и
/// вложенность: звено постфиксной цепочки ('.k', '[i]') и каждый оператор
/// левоассоциативной цепочки ('||', '&&', '+'/'-', '*'/'/'/'%'). Без этого
/// цепочка любой длины разбиралась бы успешно и роняла бы вычислитель здесь
/// (docs/grammar.md Приложение C.1).
bool eval(const Ast &ast, std::string_view source, NodeId node, Execution &exec, Value *out, Diagnostic &diag) {
    switch (ast.kind(node)) {
        case NodeKind::Number:
            *out = Value::number(ast.numberValue(node));
            return true;

        case NodeKind::Boolean:
            *out = Value::boolean(ast.boolValue(node));
            return true;

        case NodeKind::Null:
            *out = Value::null();
            return true;

        case NodeKind::String: {
            // Байты уложены в box, которым владеет это дерево, на
            // компиляции, вместе с раскодированным экранированием
            // (core/src/compile.hpp, Ast::internLiteral). Здесь остаётся
            // собрать значение из готового узла — ровно как берётся число
            // из узла Number строкой выше. Раньше на каждом вычислении
            // заводился черновик и байты собирались заново, а черновик
            // поштучно не освобождался: выражение со строкой растило память
            // на каждом кадре (docs/backlog.md B51).
            //
            // The box is owned by the Ast that parsed the literal
            // (Ast::internLiteral); the Value built here takes no reference
            // of its own. It borrows: the box is valid from this point until
            // the owning Ast is destroyed or reset, and this Value must not
            // outlive that Ast unless it is retained first
            // (chupa_value_retain).
            detail::StringBox *literal = ast.stringLiteral(node);
            *out = Value::string(literal);
            return true;
        }

        case NodeKind::Identifier: {
            // docs/semantics.md §7.1: объявлений в языке нет, всякий
            // идентификатор — чтение из хранилища.
            //
            // Имя здесь больше не ищется: check уже разрешил его в номер
            // ячейки и положил в узел, а неизвестное имя до вычисления не
            // доходит вовсе — компиляция отвергает его ошибкой Name. Отметка
            // прохода check утверждается ниже по функции, поэтому номер тут
            // заведомо проставлен.
            *out = exec.store().globalValueAt(ast.globalValuesSlot(node));
            return true;
        }

        case NodeKind::Member: {
            Value base = Value::null();
            if (!eval(ast, source, ast.child(node, 0), exec, &base, diag)) { return false; }
            // Имя поля берётся из узла буквально, без приведения.
            return readKey(ast, node, base, ast.text(node, source), out, diag);
        }

        case NodeKind::Index: {
            Value base = Value::null();
            if (!eval(ast, source, ast.child(node, 0), exec, &base, diag)) { return false; }
            // Индекс вычисляется даже при базе null: порядок зафиксирован
            // (docs/semantics.md §3.3), а короткого замыкания у индексации нет.
            Value subscript = Value::null();
            if (!eval(ast, source, ast.child(node, 1), exec, &subscript, diag)) {
                return false;
            }

            switch (base.kind()) {
                case Value::Kind::Array:
                    return readIndex(ast, node, base, subscript, out, diag);
                case Value::Kind::Object: {
                    char buffer[kNumberBufferSize];
                    std::string_view key;
                    if (!coerceToString(ast, node, subscript, buffer, &key,
                                        diag)) {
                        return false;
                    }
                    return readKey(ast, node, base, key, out, diag);
                }
                case Value::Kind::Null:
                    *out = Value::null();
                    return true;
                default:
                    return fail(ast, node, ErrorCode::Type,
                                "only arrays and objects can be indexed", diag);
            }
        }

        case NodeKind::Array: {
            const std::uint32_t count = ast.childCount(node);
            // A literal always builds a new array. An element may come from
            // an existing aggregate — `[state.header]` — and that is fine:
            // the new array takes a reference, not a copy, and every value it
            // can hold is already a self-contained box.
            // Размер известен заранее — точное выделение, без переездов.
            const Value array = CS::makeArray(count, exec.deferred());
            for (std::uint32_t i = 0; i < count; ++i) {
                Value element = Value::null();
                if (!eval(ast, source, ast.child(node, i), exec, &element, diag)) {
                    return false;
                }
                arrayPush(array, element);
            }
            *out = array;
            return true;
        }

        case NodeKind::Object: {
            // Дети чередуются: ключ, значение. Ключ — строковый литерал по
            // грамматике, приведение §4 к нему не применяется.
            const std::uint32_t count = ast.childCount(node);
            const Value object = CS::makeObject(exec.keys(), count / 2, exec.deferred());
            for (std::uint32_t i = 0; i + 1 < count; i += 2) {
                Value value = Value::null();
                if (!eval(ast, source, ast.child(node, i + 1), exec, &value, diag)) {
                    return false;
                }
                // Ключ берётся уложенным, а не разбирается заново: раскодировать
                // экранирование на каждом вычислении незачем. Байты лежат в
                // узле литерала, которым владеет хранилище контекста, и живут
                // дольше всякого объекта, который их примет.
                objectSet(object, ast.stringLiteral(ast.child(node, i))->view(),
                          value, exec.deferred());
            }
            *out = object;
            return true;
        }

        case NodeKind::Unary: {
            Value operand = Value::null();
            if (!eval(ast, source, ast.child(node, 0), exec, &operand, diag)) { return false; }
            return applyUnary(ast.op(node), operand, ast.offset(node), out, diag);
        }

        case NodeKind::Binary: {
            const TokenKind op = ast.op(node);
            // Короткое замыкание решает, вычислять ли правый операнд, поэтому
            // applyBinary этими операторами не занимается: к моменту его
            // вызова оба операнда уже вычислены, а тут именно этого делать
            // нельзя.
            if (op == TokenKind::AndAnd || op == TokenKind::OrOr) {
                Value lhs = Value::null();
                if (!eval(ast, source, ast.child(node, 0), exec, &lhs, diag)) { return false; }
                if (lhs.kind() != Value::Kind::Boolean) {
                    return fail(ast, node, ErrorCode::Type,
                                "logical operators require booleans", diag);
                }

                // Левый определил результат — правый не вычисляется, а значит и
                // не проверяется: проверять нечего. Поэтому false && 5 даёт
                // false, а true && 5 — ошибку.
                const bool left = lhs.booleanValue();
                if ((op == TokenKind::AndAnd && !left) ||
                    (op == TokenKind::OrOr && left)) {
                    *out = Value::boolean(left);
                    return true;
                }

                Value rhs = Value::null();
                if (!eval(ast, source, ast.child(node, 1), exec, &rhs, diag)) { return false; }
                if (rhs.kind() != Value::Kind::Boolean) {
                    return fail(ast, node, ErrorCode::Type,
                                "logical operators require booleans", diag);
                }
                *out = Value::boolean(rhs.booleanValue());
                return true;
            }

            if (op == TokenKind::QuestionQuestion) {
                Value lhs = Value::null();
                if (!eval(ast, source, ast.child(node, 0), exec, &lhs, diag)) { return false; }
                // Перехватывает только null: ошибка слева уже вернулась выше и
                // не гасится.
                if (lhs.kind() != Value::Kind::Null) {
                    *out = lhs;
                    return true;
                }
                return eval(ast, source, ast.child(node, 1), exec, out, diag);
            }

            // Слева направо: порядок зафиксирован (docs/semantics.md §3.3).
            Value lhs = Value::null();
            if (!eval(ast, source, ast.child(node, 0), exec, &lhs, diag)) { return false; }
            Value rhs = Value::null();
            if (!eval(ast, source, ast.child(node, 1), exec, &rhs, diag)) { return false; }
            return applyBinary(op, lhs, rhs, ast.offset(node), out, diag);
        }

        case NodeKind::Conditional: {
            Value condition = Value::null();
            if (!eval(ast, source, ast.child(node, 0), exec, &condition, diag)) { return false; }
            if (condition.kind() != Value::Kind::Boolean) {
                return fail(ast, node, ErrorCode::Type,
                            "ternary condition must be a boolean", diag);
            }
            // Вычисляется только выбранная ветвь (docs/semantics.md §5.7).
            const NodeId branch = ast.child(node, condition.booleanValue() ? 1 : 2);
            return eval(ast, source, branch, exec, out, diag);
        }

        case NodeKind::Call: {
            // Имя разрешено на компиляции (core/src/check.cpp): в узле лежит
            // готовая функция. Раньше здесь на каждом вычислении звался
            // findBuiltin — двоичный поиск по таблице со сравнением байт, — и
            // вместе с ним из исходника читался текст имени (docs/backlog.md
            // B54). Неизвестное имя до вычисления не доходит: check отвергает
            // его ошибкой Name, а отметку прохода утверждает evalExpression.
            const Builtin id = builtinOfCallee(ast.callee(node));
            // format вариадичен, и буфер аргументов ниже на него не рассчитан:
            // он вычисляет аргументы по мере надобности и придёт своим путём
            // (core/src/eval.cpp, задача 6). assert тут не годится — в
            // релизной сборке он исчезает, а переполнение буфера остаётся:
            // check пропускает format с любым числом аргументов, и
            // format('${}...', 1, 2, 3, 4, 5) иначе переполнил бы args[2].
            if (id == Builtin::Format) { return evalFormat(ast, source, node, exec, out, diag); }

            // Арность гарантирована проходом, а размер буфера — таблицей
            // билтинов (core/src/builtin.hpp::kMaxFixedArgs), а не догадкой:
            // static_assert в builtin.cpp не даст завести функцию с большей
            // фиксированной арностью, не расширив следом и этот буфер.
            // Инициализатор ниже перечисляет ровно kMaxFixedArgs элементов
            // явно: у Value фабрики закрыты (value.hpp), заполнить массив
            // циклом или {} снаружи класса нечем. static_assert ниже не даёт
            // списку молча разойтись со значением kMaxFixedArgs, если оно
            // когда-нибудь изменится.
            static_assert(kMaxFixedArgs == 2,
                          "kMaxFixedArgs изменился — дополните список "
                          "инициализаторов args ниже до того же числа элементов");
            Value args[kMaxFixedArgs] = {Value::null(), Value::null()};
            const std::uint32_t count = ast.childCount(node);
            for (std::uint32_t i = 0; i < count; ++i) {
                if (!eval(ast, source, ast.child(node, i), exec, &args[i], diag)) {
                    return false;
                }
            }
            return applyBuiltin(id, exec, args, count, ast.offset(node),
                                out, diag);
        }

        default:
            // Операторы и цепочки доступа разобраны выше отдельными ветками.
            // Сюда попадают узлы, которых в дереве от parseExpression быть не
            // может: Script, Assign, CallStatement — стейтменты, а не
            // выражения.
            return fail(ast, node, ErrorCode::Type,
                        "expression form is not supported", diag);
    }
}

/// Соответствие составного оператора обычному (docs/semantics.md §7.3).
///
/// Операции %= в языке нет (docs/grammar.md §5.2).
TokenKind compoundOperation(TokenKind op) {
    switch (op) {
        case TokenKind::PlusAssign: return TokenKind::Plus;
        case TokenKind::MinusAssign: return TokenKind::Minus;
        case TokenKind::StarAssign: return TokenKind::Star;
        case TokenKind::SlashAssign: return TokenKind::Slash;
        default:
            assert(false && "не составной оператор присваивания");
            return TokenKind::Plus;
    }
}

/// Присваивание по имени поля: base.k = v.
///
/// Порядок вычисления — подвыражения цели, затем правая часть
/// (docs/semantics.md §7.2).
bool assignToKey(const Ast &ast, std::string_view source, NodeId node,
                 NodeId target, Execution &exec, Diagnostic &diag) {
    Value base = Value::null();
    if (!eval(ast, source, ast.child(target, 0), exec, &base, diag)) { return false; }

    Value value = Value::null();
    if (!eval(ast, source, ast.child(node, 1), exec, &value, diag)) { return false; }

    // Запись в null — ошибка: мягкость §6.3 распространяется только на чтение,
    // а молчаливо пропущенная запись потеряла бы данные без следа.
    if (base.kind() != Value::Kind::Object) {
        return fail(ast, target, ErrorCode::Type, "only objects have keys",
                    diag);
    }

    // Имя поля берётся из узла буквально, как при чтении (§6.2). Это срез
    // исходника, а не пула, поэтому запись в хранилище его не задевает.
    const std::string_view key = ast.text(target, source);

    const TokenKind op = ast.op(node);
    if (op != TokenKind::Assign) {
        // x op= e есть x = x op e. Чтение идёт по уже вычисленной базе,
        // поэтому подвыражения цели вычислены ровно один раз
        // (docs/grammar.md §6.4).
        const Value current = CS::objectGet(base, key);
        Value combined = Value::null();
        if (!applyBinary(compoundOperation(op), current, value,
                         ast.offset(node), &combined, diag)) {
            return false;
        }
        value = combined;
    }

    objectSet(base, key, value, exec.deferred());
    return true;
}

/// Присваивание по индексу: base[i] = v.
bool assignToIndex(const Ast &ast, std::string_view source, NodeId node,
                   NodeId target, Execution &exec, Diagnostic &diag) {
    // Порядок: база, индекс, затем правая часть (docs/semantics.md §7.2).
    Value base = Value::null();
    if (!eval(ast, source, ast.child(target, 0), exec, &base, diag)) { return false; }
    Value subscript = Value::null();
    if (!eval(ast, source, ast.child(target, 1), exec, &subscript, diag)) { return false; }

    Value value = Value::null();
    if (!eval(ast, source, ast.child(node, 1), exec, &value, diag)) { return false; }

    switch (base.kind()) {
        case Value::Kind::Array: {
            // Требования к индексу те же, что при чтении (§6.1).
            double index = 0.0;
            if (!checkArrayIndex(ast, target, subscript, &index, diag)) {
                return false;
            }


            const TokenKind op = ast.op(node);
            if (op != TokenKind::Assign) {
                // Чтение за границей штатно даёт null, поэтому items[5] += 1
                // упирается не в границу записи, а в сложение с null: Type, а
                // не Range (§7.3).
                Value current = Value::null();
                if (!readIndex(ast, target, base, subscript, &current, diag)) {
                    return false;
                }
                Value combined = Value::null();
                if (!applyBinary(compoundOperation(op), current, value,
                                 ast.offset(node), &combined, diag)) {
                    return false;
                }
                value = combined;
            }

            // Запись за границу — ошибка: расширяет только push (§6.1).
            // Сравнение в double, потому что индекс может превышать uint32.
            if (index >= static_cast<double>(CS::arrayCount(base))) {
                return fail(ast, target, ErrorCode::Range,
                            "array index is out of bounds", diag);
            }
            // Границу проверили выше, поэтому запись не отказывает.
            static_cast<void>(arraySet(base, static_cast<std::uint32_t>(index),
                                       value, exec.deferred()));
            return true;
        }

        case Value::Kind::Object: {
            const TokenKind op = ast.op(node);

            char buffer[kNumberBufferSize];
            std::string_view key;
            if (!coerceToString(ast, target, subscript, buffer, &key,
                                diag)) {
                return false;
            }

            if (op != TokenKind::Assign) {
                const Value current = CS::objectGet(base, key);
                Value combined = Value::null();
                if (!applyBinary(compoundOperation(op), current, value,
                                 ast.offset(node), &combined, diag)) {
                    return false;
                }
                // applyBinary only ever returns a Number or a Boolean for a
                // compound assignment (docs/semantics.md §7.2); asserted, not
                // silently assumed, so a future arithmetic result that
                // referenced a box would fail loudly here instead of eval.cpp
                // silently mishandling it.
                assert(!combined.referencesBox() &&
                       "compound assignment produced a box-referencing value");
                value = combined;
            }

            objectSet(base, key, value, exec.deferred());
            return true;
        }

        default:
            // Запись в null — ошибка, как и по имени поля.
            return fail(ast, target, ErrorCode::Type,
                        "only arrays and objects can be assigned by index",
                        diag);
    }
}

/// Присваивание: разбирает форму цели и передаёт дальше.
bool assign(const Ast &ast, std::string_view source, NodeId node,
            Execution &exec, Diagnostic &diag) {
    const NodeId target = ast.child(node, 0);
    switch (ast.kind(target)) {
        case NodeKind::Member:
            return assignToKey(ast, source, node, target, exec, diag);

        case NodeKind::Index:
            return assignToIndex(ast, source, node, target, exec, diag);

        default:
            // Грамматика строит целью Identifier, Member и Index
            // (docs/grammar.md §5.2). Identifier как цель отсеян статическим
            // проходом (core/src/check.hpp, docs/semantics.md §7.2) — дерево,
            // дошедшее до вычислителя, эту форму содержать не может, и
            // защитная ветка покрывает её наравне с прочими невозможными.
            return fail(ast, target, ErrorCode::Type,
                        "invalid assignment target", diag);
    }
}

/// Выполняет один стейтмент.
bool execute(const Ast &ast, std::string_view source, NodeId node,
             Execution &exec, Diagnostic &diag) {
    switch (ast.kind(node)) {
        case NodeKind::Assign:
            return assign(ast, source, node, exec, diag);

        case NodeKind::CallStatement: {
            // Вызов в позиции стейтмента возвращает Void, поэтому результат
            // читать нечего и незачем (docs/semantics.md §2.2).
            Value discarded = Value::null();
            return eval(ast, source, ast.child(node, 0), exec, &discarded, diag);
        }

        default:
            // Грамматика строит стейтмент только как Assign либо
            // CallStatement (docs/grammar.md §5.1); обе разобраны выше —
            // ветка защитная.
            return fail(ast, node, ErrorCode::Type,
                        "statement form is not supported", diag);
    }
}

}  // namespace

bool evalExpression(const Ast &ast, std::string_view source, Execution &exec,
                    Value *out, Diagnostic &diag) {
    assert(ast.root() != kNoNode && "дерево обязано быть разобрано успешно");
    assert(ast.isChecked() && "дерево обязано пройти check перед вычислением");
    return eval(ast, source, ast.root(), exec, out, diag);
}

bool runScript(const Ast &ast, std::string_view source, Execution &exec,
               Diagnostic &diag) {
    assert(ast.root() != kNoNode && "дерево обязано быть разобрано успешно");
    assert(ast.isChecked() && "дерево обязано пройти check перед вычислением");
    const NodeId script = ast.root();
    assert(ast.kind(script) == NodeKind::Script &&
           "runScript ждёт дерево от parseScript");

    const std::uint32_t count = ast.childCount(script);
    for (std::uint32_t i = 0; i < count; ++i) {
        // Ошибка прерывает выполнение, а сделанное остаётся сделанным:
        // откатывать нечего.
        if (!execute(ast, source, ast.child(script, i), exec, diag)) { return false; }
    }
    return true;
}

}  // namespace CS
