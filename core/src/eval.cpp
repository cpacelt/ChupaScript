#include "eval.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "builtin.hpp"
#include "operator.hpp"
#include "text.hpp"

namespace CS {
namespace {

// Предварительное объявление для readKey
bool eval(const Ast &ast, NodeId node, Context &ctx, Value *out,
          Diagnostic &diag);

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
bool readKey(const Ast &ast, NodeId node, Context &ctx, Value base,
             std::string_view key, Value *out, Diagnostic &diag) {
    switch (base.kind()) {
        case Value::Kind::Object:
            *out = ctx.objectGet(base, key);
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
/// объекта, и has, и (задачами 5 и 6) str с format.
///
/// Возвращает срез: у строки — её собственные байты в контексте, у числа —
/// буфер вызывающего, у остальных — статическая строка. В контекст ничего не
/// кладётся: строка нужна на время одного поиска ключа, и класть её в пул
/// значило бы копить мусор на каждом чтении.
///
/// Срез из ветки String действителен лишь до ближайшей мутации контекста — с
/// тем же сроком жизни и по той же причине, что и результат Context::string.
/// У потребителей этой ветки два разных пути с этим сроком: readKey (через
/// const objectGet) обращается к срезу до всякой мутации, а assignToIndex
/// передаёт его дальше в мутирующий ctx.objectSet. Второй путь безопасен по
/// двум причинам, и обе обязаны выполняться разом:
///
/// - Context::appendText (core/src/context.cpp) явно распознаёт срез,
///   указывающий внутрь пула text_, и после роста пула копирует из нового
///   расположения по запомненному смещению, а не по повисшему указателю —
///   это поведение закреплено тестом в core/tests/context_test.cpp;
/// - между вызовом coerceToString и вызовом objectSet контекст не мутирует,
///   потому что applyBinary принимает `const Context &` (core/src/operator.hpp)
///   и залезть в text_ не может.
///
/// Вторая опора хрупкая: если applyBinary когда-нибудь получит изменяемый
/// контекст, срез повиснет ещё до вызова objectSet, и защита appendText уже
/// не поможет — она признаёт срез, указывающий в актуальный пул, а не чинит
/// произвольно устаревший указатель.
///
/// numberBuffer обязан быть размером не меньше kNumberBufferSize.
bool coerceToString(const Ast &ast, NodeId node, Context &ctx, Value value,
                    char *numberBuffer, std::string_view *out,
                    Diagnostic &diag) {
    return coerceScalarToString(ctx, value, numberBuffer, out,
                                ast.offset(node), diag);
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
bool readIndex(const Ast &ast, NodeId node, Context &ctx, Value array,
               Value subscript, Value *out, Diagnostic &diag) {
    double index = 0.0;
    if (!checkArrayIndex(ast, node, subscript, &index, diag)) { return false; }

    // За границей — штатное чтение. Сравнение в double, потому что индекс
    // может превышать всё, что влезает в uint32.
    if (index >= static_cast<double>(ctx.arrayCount(array))) {
        *out = Value::null();
        return true;
    }
    *out = ctx.arrayAt(array, static_cast<std::uint32_t>(index));
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
bool eval(const Ast &ast, NodeId node, Context &ctx, Value *out,
          Diagnostic &diag) {
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
            std::string scratch;
            *out = ctx.makeString(literalText(ast, node, scratch));
            return true;
        }

        case NodeKind::Identifier: {
            // docs/semantics.md §7.1: объявлений в языке нет, всякий
            // идентификатор — чтение из контекста.
            const std::string_view name = ast.text(node);
            // Неизвестный корень — ошибка, а не null: состав корней контексту
            // известен, состав ключей внутри них — нет. Поэтому опечатка в
            // корне ловится, а опечатка глубже даёт null по §6.3.
            if (!ctx.hasRoot(name)) {
                return fail(ast, node, ErrorCode::Name, "unknown root", diag);
            }
            *out = ctx.root(name);
            return true;
        }

        case NodeKind::Member: {
            Value base = Value::null();
            if (!eval(ast, ast.child(node, 0), ctx, &base, diag)) { return false; }
            // Имя поля берётся из узла буквально, без приведения.
            return readKey(ast, node, ctx, base, ast.text(node), out, diag);
        }

        case NodeKind::Index: {
            Value base = Value::null();
            if (!eval(ast, ast.child(node, 0), ctx, &base, diag)) { return false; }
            // Индекс вычисляется даже при базе null: порядок зафиксирован
            // (docs/semantics.md §3.3), а короткого замыкания у индексации нет.
            Value subscript = Value::null();
            if (!eval(ast, ast.child(node, 1), ctx, &subscript, diag)) {
                return false;
            }

            switch (base.kind()) {
                case Value::Kind::Array:
                    return readIndex(ast, node, ctx, base, subscript, out, diag);
                case Value::Kind::Object: {
                    char buffer[kNumberBufferSize];
                    std::string_view key;
                    if (!coerceToString(ast, node, ctx, subscript, buffer, &key,
                                        diag)) {
                        return false;
                    }
                    return readKey(ast, node, ctx, base, key, out, diag);
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
            // Размер известен заранее — точное выделение, без переездов.
            const Value array = ctx.makeArray(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                Value element = Value::null();
                if (!eval(ast, ast.child(node, i), ctx, &element, diag)) {
                    return false;
                }
                ctx.arrayPush(array, element);
            }
            *out = array;
            return true;
        }

        case NodeKind::Object: {
            // Дети чередуются: ключ, значение. Ключ — строковый литерал по
            // грамматике, приведение §4 к нему не применяется.
            const std::uint32_t count = ast.childCount(node);
            const Value object = ctx.makeObject(count / 2);
            std::string scratch;
            for (std::uint32_t i = 0; i + 1 < count; i += 2) {
                Value value = Value::null();
                if (!eval(ast, ast.child(node, i + 1), ctx, &value, diag)) {
                    return false;
                }
                ctx.objectSet(object,
                              literalText(ast, ast.child(node, i), scratch),
                              value);
            }
            *out = object;
            return true;
        }

        case NodeKind::Unary: {
            Value operand = Value::null();
            if (!eval(ast, ast.child(node, 0), ctx, &operand, diag)) { return false; }
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
                if (!eval(ast, ast.child(node, 0), ctx, &lhs, diag)) { return false; }
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
                if (!eval(ast, ast.child(node, 1), ctx, &rhs, diag)) { return false; }
                if (rhs.kind() != Value::Kind::Boolean) {
                    return fail(ast, node, ErrorCode::Type,
                                "logical operators require booleans", diag);
                }
                *out = Value::boolean(rhs.booleanValue());
                return true;
            }

            if (op == TokenKind::QuestionQuestion) {
                Value lhs = Value::null();
                if (!eval(ast, ast.child(node, 0), ctx, &lhs, diag)) { return false; }
                // Перехватывает только null: ошибка слева уже вернулась выше и
                // не гасится.
                if (lhs.kind() != Value::Kind::Null) {
                    *out = lhs;
                    return true;
                }
                return eval(ast, ast.child(node, 1), ctx, out, diag);
            }

            // Слева направо: порядок зафиксирован (docs/semantics.md §3.3).
            Value lhs = Value::null();
            if (!eval(ast, ast.child(node, 0), ctx, &lhs, diag)) { return false; }
            Value rhs = Value::null();
            if (!eval(ast, ast.child(node, 1), ctx, &rhs, diag)) { return false; }
            return applyBinary(op, lhs, rhs, ctx, ast.offset(node), out, diag);
        }

        case NodeKind::Conditional: {
            Value condition = Value::null();
            if (!eval(ast, ast.child(node, 0), ctx, &condition, diag)) { return false; }
            if (condition.kind() != Value::Kind::Boolean) {
                return fail(ast, node, ErrorCode::Type,
                            "ternary condition must be a boolean", diag);
            }
            // Вычисляется только выбранная ветвь (docs/semantics.md §5.7).
            const NodeId branch = ast.child(node, condition.booleanValue() ? 1 : 2);
            return eval(ast, branch, ctx, out, diag);
        }

        case NodeKind::Call: {
            Builtin id = Builtin::Count;
            const bool known = findBuiltin(ast.text(node), &id);
            // Неизвестное имя отсеял статический проход, а дереву мы доверяем
            // (спека §5.3): здесь это утверждение, а не диагностика.
            assert(known && "дерево обязано пройти check");
            (void)known;

            // format вариадичен, и буфер аргументов ниже на него не рассчитан:
            // он вычисляет аргументы по мере надобности и придёт своим путём
            // (core/src/eval.cpp, задача 6). assert тут не годится — в
            // релизной сборке он исчезает, а переполнение буфера остаётся:
            // check пропускает format с любым числом аргументов, и
            // format('${}...', 1, 2, 3, 4, 5) иначе переполнил бы args[2].
            if (id == Builtin::Format) {
                return fail(ast, node, ErrorCode::Type,
                            "builtin is not implemented yet", diag);
            }

            // Арность гарантирована проходом, поэтому буфер по самой широкой
            // невариадической функции — двум аргументам.
            Value args[2] = {Value::null(), Value::null()};
            const std::uint32_t count = ast.childCount(node);
            for (std::uint32_t i = 0; i < count; ++i) {
                if (!eval(ast, ast.child(node, i), ctx, &args[i], diag)) {
                    return false;
                }
            }
            return applyBuiltin(id, ctx, args, count, ast.offset(node), out, diag);
        }

        default:
            // Операторы и цепочки доступа разобраны выше отдельными ветками.
            // Сюда попадают узлы, которых в дереве от parseExpression быть не
            // может: Program, Assign, CallStatement — стейтменты, а не
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
bool assignToKey(const Ast &ast, NodeId node, NodeId target, Context &ctx,
                 Diagnostic &diag) {
    Value base = Value::null();
    if (!eval(ast, ast.child(target, 0), ctx, &base, diag)) { return false; }

    Value value = Value::null();
    if (!eval(ast, ast.child(node, 1), ctx, &value, diag)) { return false; }

    // Запись в null — ошибка: мягкость §6.3 распространяется только на чтение,
    // а молчаливо пропущенная запись потеряла бы данные без следа.
    if (base.kind() != Value::Kind::Object) {
        return fail(ast, target, ErrorCode::Type, "only objects have keys",
                    diag);
    }

    // Имя поля берётся из узла буквально, как при чтении (§6.2).
    const std::string_view key = ast.text(target);

    const TokenKind op = ast.op(node);
    if (op != TokenKind::Assign) {
        // x op= e есть x = x op e. Чтение идёт по уже вычисленной базе,
        // поэтому подвыражения цели вычислены ровно один раз
        // (docs/grammar.md §6.4).
        const Value current = ctx.objectGet(base, key);
        Value combined = Value::null();
        if (!applyBinary(compoundOperation(op), current, value, ctx,
                         ast.offset(node), &combined, diag)) {
            return false;
        }
        value = combined;
    }

    ctx.objectSet(base, key, value);
    return true;
}

/// Присваивание по индексу: base[i] = v.
bool assignToIndex(const Ast &ast, NodeId node, NodeId target, Context &ctx,
                   Diagnostic &diag) {
    // Порядок: база, индекс, затем правая часть (docs/semantics.md §7.2).
    Value base = Value::null();
    if (!eval(ast, ast.child(target, 0), ctx, &base, diag)) { return false; }
    Value subscript = Value::null();
    if (!eval(ast, ast.child(target, 1), ctx, &subscript, diag)) { return false; }

    Value value = Value::null();
    if (!eval(ast, ast.child(node, 1), ctx, &value, diag)) { return false; }

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
                if (!readIndex(ast, target, ctx, base, subscript, &current,
                               diag)) {
                    return false;
                }
                Value combined = Value::null();
                if (!applyBinary(compoundOperation(op), current, value, ctx,
                                 ast.offset(node), &combined, diag)) {
                    return false;
                }
                value = combined;
            }

            // Запись за границу — ошибка: расширяет только push (§6.1).
            // Сравнение в double, потому что индекс может превышать uint32.
            if (index >= static_cast<double>(ctx.arrayCount(base))) {
                return fail(ast, target, ErrorCode::Range,
                            "array index is out of bounds", diag);
            }
            // Границу проверили выше, поэтому запись не отказывает.
            static_cast<void>(
                ctx.arraySet(base, static_cast<std::uint32_t>(index), value));
            return true;
        }

        case Value::Kind::Object: {
            char buffer[kNumberBufferSize];
            std::string_view key;
            if (!coerceToString(ast, target, ctx, subscript, buffer, &key,
                                diag)) {
                return false;
            }

            const TokenKind op = ast.op(node);
            if (op != TokenKind::Assign) {
                const Value current = ctx.objectGet(base, key);
                Value combined = Value::null();
                if (!applyBinary(compoundOperation(op), current, value, ctx,
                                 ast.offset(node), &combined, diag)) {
                    return false;
                }
                value = combined;
            }

            ctx.objectSet(base, key, value);
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
bool assign(const Ast &ast, NodeId node, Context &ctx, Diagnostic &diag) {
    const NodeId target = ast.child(node, 0);
    switch (ast.kind(target)) {
        case NodeKind::Member:
            return assignToKey(ast, node, target, ctx, diag);

        case NodeKind::Index:
            return assignToIndex(ast, node, target, ctx, diag);

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
bool execute(const Ast &ast, NodeId node, Context &ctx, Diagnostic &diag) {
    switch (ast.kind(node)) {
        case NodeKind::Assign:
            return assign(ast, node, ctx, diag);

        case NodeKind::CallStatement: {
            // Вызов в позиции стейтмента возвращает Void, поэтому результат
            // читать нечего и незачем (docs/semantics.md §2.2).
            Value discarded = Value::null();
            return eval(ast, ast.child(node, 0), ctx, &discarded, diag);
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

bool evalExpression(const Ast &ast, Context &ctx, Value *out,
                    Diagnostic &diag) {
    assert(ast.root() != kNoNode && "дерево обязано быть разобрано успешно");
    assert(ast.isChecked() && "дерево обязано пройти check перед вычислением");
    return eval(ast, ast.root(), ctx, out, diag);
}

bool runScript(const Ast &ast, Context &ctx, Diagnostic &diag) {
    assert(ast.root() != kNoNode && "дерево обязано быть разобрано успешно");
    assert(ast.isChecked() && "дерево обязано пройти check перед вычислением");
    const NodeId program = ast.root();
    assert(ast.kind(program) == NodeKind::Program &&
           "runScript ждёт дерево от parseProgram");

    const std::uint32_t count = ast.childCount(program);
    for (std::uint32_t i = 0; i < count; ++i) {
        // Ошибка прерывает выполнение, а сделанное остаётся сделанным:
        // откатывать нечего.
        if (!execute(ast, ast.child(program, i), ctx, diag)) { return false; }
    }
    return true;
}

}  // namespace CS
