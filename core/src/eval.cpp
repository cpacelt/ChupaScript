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
/// объекта, и has, и (задачами 5 и 6) str с format.
///
/// Возвращает срез: у строки — её собственные байты в хранилище, у числа —
/// буфер вызывающего, у остальных — статическая строка. В хранилище ничего не
/// кладётся: строка нужна на время одного поиска ключа, и класть её в пул
/// значило бы копить мусор на каждом чтении.
///
/// Срез из ветки String действителен лишь до ближайшей мутации хранилища — с
/// тем же сроком жизни и по той же причине, что и результат Store::string.
/// У потребителей этой ветки два разных пути с этим сроком: readKey (через
/// const objectGet) обращается к срезу до всякой мутации, а assignToIndex
/// передаёт его дальше в мутирующий store.objectSet. Второй путь безопасен по
/// двум причинам, и обе обязаны выполняться разом:
///
/// - Store::appendText (core/src/store.cpp) явно распознаёт срез,
///   указывающий внутрь пула text_, и после роста пула копирует из нового
///   расположения по запомненному смещению, а не по повисшему указателю —
///   это поведение закреплено тестом в core/tests/store_test.cpp;
/// - между вызовом coerceToString и вызовом objectSet хранилище не мутирует,
///   потому что applyBinary принимает `const Store &` (core/src/operator.hpp)
///   и залезть в text_ не может.
///
/// Вторая опора хрупкая: если applyBinary когда-нибудь получит изменяемый
/// хранилище, срез повиснет ещё до вызова objectSet, и защита appendText уже
/// не поможет — она признаёт срез, указывающий в актуальный пул, а не чинит
/// произвольно устаревший указатель.
///
/// numberBuffer обязан быть размером не меньше kNumberBufferSize.
bool coerceToString(const Ast &ast, NodeId node, const Store &store, Value value,
                    char *numberBuffer, std::string_view *out,
                    Diagnostic &diag) {
    return coerceScalarToString(store, value, numberBuffer, out,
                                ast.offset(node), diag);
}

/// Собирает строку по шаблону (docs/semantics.md §8.8).
///
/// Живёт здесь, а не в builtin.cpp, потому что format вариадичен: буфер под
/// заранее вычисленные аргументы потребовал бы верхней границы их числа,
/// которой §8.8 не устанавливает. Шаблон потребляет аргументы строго слева
/// направо, по одному на плейсхолдер, поэтому лениво выходит и проще, и без
/// придуманного предела.
///
/// Разбор шаблона — через nextFormatPiece (builtin.hpp): это та же функция,
/// которой статический проход (check.cpp) считает плейсхолдеры у литерального
/// шаблона, так что правило «$${} — литерал, ${} — плейсхолдер» записано один
/// раз и держит обоих потребителей синхронными не по договорённости, а по
/// устройству.
///
/// Срез шаблона берётся заново на каждый вызов nextFormatPiece, а не один раз
/// до цикла: вычисление аргумента-плейсхолдера (eval ниже) вправе само писать
/// в пул текста — строковый литерал зовёт makeString, вложенный format
/// завершает свою сборку в тот же пул, — и это может переселить text_ в новую
/// память. Смещение, на которое указывает tmpl, при переезде остаётся верным
/// (Store::string пересчитывает срез из смещения и длины), а вот кэшированный
/// указатель — уже нет. Свежий вызов string(tmpl) перед каждым обращением к
/// шаблону — единственный способ не держать такой указатель через границу, за
/// которой могла случиться запись.
///
/// Разделение регионов эту опасность не сняло, а сузило: шаблон-литерал лежит
/// в постоянном пуле, а сборка идёт во временном, и тогда переселять его
/// нечему. Но вычисленный шаблон (`format(tpl.two, 1, 2)` после [B57] — всё
/// ещё постоянный, а `format(format(...), x)` — уже временный) попадает в тот
/// же пул, куда пишет сборка, и случай возвращается целиком.
bool evalFormat(const Ast &ast, std::string_view source, NodeId node,
                Execution &exec, Value *out, Diagnostic &diag) {
    const std::uint32_t argCount = ast.childCount(node);

    Value tmpl = Value::null();
    if (!eval(ast, source, ast.child(node, 0), exec, &tmpl, diag)) { return false; }
    if (tmpl.kind() != Value::Kind::String) {
        return fail(ast, node, ErrorCode::Type,
                    "format expects a string template", diag);
    }

    // Результат — новое значение, поэтому собирается во временном регионе.
    // Шаблон, наоборот, читается там, где лежит: литерал уложен в постоянный
    // пул на компиляции, вычисленная строка — во временном.
    Store &result = exec.scratch;
    const Store &from = exec.scratch;

    const std::uint32_t mark = result.beginString();
    std::uint32_t next = 1;  // следующий аргумент
    FormatCursor cursor;

    for (;;) {
        FormatPiece kind = FormatPiece::Literal;
        std::string_view chunk;
        if (!nextFormatPiece(from.string(tmpl), cursor, &kind, &chunk)) { break; }

        if (kind == FormatPiece::Literal) {
            result.appendToString(chunk);
            continue;
        }
        if (kind == FormatPiece::Escaped) {
            result.appendToString("${}");
            continue;
        }

        // FormatPiece::Placeholder — потребляет следующий аргумент.
        if (next >= argCount) {
            result.abortString(mark);
            return fail(ast, node, ErrorCode::Type,
                        "format placeholder count does not match arguments",
                        diag);
        }
        Value argument = Value::null();
        if (!eval(ast, source, ast.child(node, next), exec, &argument, diag)) {
            result.abortString(mark);
            return false;
        }
        ++next;

        char buffer[kNumberBufferSize];
        std::string_view text;
        if (!coerceToString(ast, node, exec.scratch, argument,
                            buffer, &text, diag)) {
            result.abortString(mark);
            return false;
        }
        result.appendToString(text);
    }

    if (next != argCount) {
        result.abortString(mark);
        return fail(ast, node, ErrorCode::Type,
                    "format placeholder count does not match arguments", diag);
    }
    *out = result.endString(mark);
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
            // Байты уложены в пул текста на компиляции, вместе с
            // раскодированным экранированием (core/src/compile.hpp). Здесь
            // остаётся собрать значение из координат — ровно как берётся число
            // из узла Number строкой выше. Раньше на каждом вычислении
            // заводился черновик и байты дописывались в пул заново, а он
            // поштучно не освобождается: выражение со строкой растило память на
            // каждом кадре (docs/backlog.md B51).
            // Литерал уложен на компиляции: он часть программы, а не
            // создаваемое значение. Узлом владеет хранилище контекста и
            // отпускает его только вместе с собой, поэтому ссылки здесь никто
            // не берёт — брать её было бы не у кого и не для кого.
            detail::StringBox *literal = ast.stringLiteral(node);
            *out = Value::string(literal, literal->len);
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
            *out = exec.persistent().globalValueAt(ast.globalValuesSlot(node));
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
                    if (!coerceToString(ast, node,
                                        exec.scratch,
                                        subscript, buffer, &key, diag)) {
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
            // Литерал создаёт новое значение, а у вычисления нет способа
            // создать долгоживущее: массив рождается во временном регионе
            // (docs/backlog.md [B57]). Элемент может прийти из постоянного —
            // `[state.header]` — и это разрешено: барьер направленный, ссылка
            // умрёт раньше того, на что указывает.
            // Размер известен заранее — точное выделение, без переездов.
            const Value array = CS::makeArray(count, exec.deferred());
            for (std::uint32_t i = 0; i < count; ++i) {
                Value element = Value::null();
                if (!eval(ast, source, ast.child(node, i), exec, &element, diag)) {
                    return false;
                }
                // Продвижение обязательно и здесь, хотя массив временный:
                // агрегат — узел и умеет уехать в глобальную переменную, а
                // смещение в арену операции туда попасть не должно.
                arrayPush(array, exec.promote(element));
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
                          exec.promote(value), exec.deferred());
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
            return applyBinary(op, lhs, rhs, exec.scratch,
                               exec.scratch, ast.offset(node), out,
                               diag);
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
            const Builtin id = ast.builtinId(node);
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

    // Писать надо туда, где лежит цель: state.k = ... в постоянное,
    // [{'k': 1}][0].k = ... во временное (docs/backlog.md [B57]).

    const TokenKind op = ast.op(node);
    if (op != TokenKind::Assign) {
        // x op= e есть x = x op e. Чтение идёт по уже вычисленной базе,
        // поэтому подвыражения цели вычислены ровно один раз
        // (docs/grammar.md §6.4).
        const Value current = CS::objectGet(base, key);
        Value combined = Value::null();
        if (!applyBinary(compoundOperation(op), current, value,
                         exec.scratch, exec.scratch, ast.offset(node),
                         &combined, diag)) {
            return false;
        }
        value = combined;
    }

    // Барьер записи: временное значение в постоянном агрегате пережило бы
    // сброс своего региона, поэтому копируется. Обратное направление promote
    // пропускает как есть.
    objectSet(base, key, exec.promote(value), exec.deferred());
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
                                 exec.scratch, exec.scratch, ast.offset(node),
                                 &combined, diag)) {
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
                                       exec.promote(value), exec.deferred()));
            return true;
        }

        case Value::Kind::Object: {
            const TokenKind op = ast.op(node);

            // Продвижение стоит **до** приведения ключа, а не рядом с записью.
            // Ключ — срез пула текста, возможно того же самого, куда promote
            // допишет копию; взятый раньше, он повис бы ровно так, как
            // предупреждает комментарий у coerceToString. Порядок здесь и есть
            // защита.
            if (op == TokenKind::Assign) { value = exec.promote(value); }

            char buffer[kNumberBufferSize];
            std::string_view key;
            if (!coerceToString(ast, target, exec.scratch,
                                subscript, buffer, &key, diag)) {
                return false;
            }

            if (op != TokenKind::Assign) {
                const Value current = CS::objectGet(base, key);
                Value combined = Value::null();
                if (!applyBinary(compoundOperation(op), current, value,
                                 exec.scratch, exec.scratch, ast.offset(node),
                                 &combined, diag)) {
                    return false;
                }
                // Продвигать нечего: applyBinary отдаёт только число и флаг,
                // а они ничего не адресуют. Утверждение, а не молчаливое
                // допущение — если у += появится результат-агрегат, вместе с
                // продвижением придётся заново решать и про ключ выше.
                assert(!combined.addressesStore() &&
                       "составное присваивание дало ссылающееся значение: "
                       "его надо продвигать, а ключ — брать после этого");
                value = combined;
            }

            // Продвигать здесь нечего ни на одном из путей: при Assign
            // значение уже прошло promote выше, до приведения ключа, а при
            // составном присваивании оно результат applyBinary, и то, что оно
            // ничего не адресует, утверждается строкой выше.
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
