#include "builtin.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "text.hpp"
#include "aggregate.hpp"

namespace CS {
namespace {

/// Отсортирована по имени: findBuiltin ищет двоично, как findKey в хранилище.
/// Порядок обязан совпадать с порядком в enum Builtin — на этом стоит индексация
/// в builtinInfo, и тест BuiltinTable.IsSortedByName стережёт инвариант.
constexpr BuiltinInfo kTable[] = {
    {"abs", 1, 1, true, true, true},
    {"count", 1, 1, true, true, true},
    {"format", 1, kVariadic, true, true, true},
    {"has", 2, 2, true, true, true},
    {"keys", 1, 1, true, true, true},
    {"last", 1, 1, true, true, true},
    {"max", 2, 2, true, true, true},
    {"min", 2, 2, true, true, true},
    {"pop", 1, 1, false, false, false},
    {"push", 2, 2, false, false, false},
    {"round", 1, 1, true, true, true},
    {"str", 1, 1, true, true, true},
};

constexpr std::size_t kCount = sizeof kTable / sizeof kTable[0];
static_assert(kCount == static_cast<std::size_t>(Builtin::Str) + 1,
              "таблица и enum обязаны совпадать по составу");

/// Стережёт соответствие таблицы буферу вычислителя: функция с большей
/// фиксированной арностью переполнила бы его, пройдя все проверки.
constexpr bool fixedArityFitsBuffer() {
    for (const BuiltinInfo &info : kTable) {
        if (info.maxArgs != kVariadic && info.maxArgs > kMaxFixedArgs) {
            return false;
        }
    }
    return true;
}
static_assert(fixedArityFitsBuffer(),
              "фиксированная арность превышает kMaxFixedArgs: "
              "буфер аргументов вычислителя придётся расширить");

}  // namespace

bool findBuiltin(std::string_view name, Builtin *out) noexcept {
    const BuiltinInfo *first = kTable;
    const BuiltinInfo *last = kTable + kCount;
    const BuiltinInfo *found = std::lower_bound(
        first, last, name,
        [](const BuiltinInfo &info, std::string_view key) {
            return info.name < key;
        });
    if (found == last || found->name != name) { return false; }
    *out = static_cast<Builtin>(found - first);
    return true;
}

const BuiltinInfo &builtinInfo(Builtin id) noexcept {
    const std::size_t index = static_cast<std::size_t>(id);
    assert(index < kCount);
    return kTable[index];
}

namespace {

/// Длина маркера, начинающегося на pos: 4 — экранирование, 3 — плейсхолдер,
/// 0 — не маркер.
///
/// Экранирование проверяется первым: иначе его собственное "${}" отсеклось бы
/// как настоящий плейсхолдер.
///
/// Хвост короче образца безопасен сам собой — substr отдаст сколько есть, и
/// сравнение с более длинным образцом не сойдётся. А вот pos за пределами
/// шаблона substr не простит: он бросит out_of_range, которому в noexcept
/// уйти некуда. Единственный вызывающий ниже даёт pos внутри шаблона, и
/// assert стережёт это условие, если вызывающих станет больше.
std::size_t markerAt(std::string_view fmt, std::size_t pos) noexcept {
    assert(pos <= fmt.size() && "позиция за пределами шаблона");
    if (fmt.compare(pos, 4, "$${}") == 0) { return 4; }
    if (fmt.compare(pos, 3, "${}") == 0) { return 3; }
    return 0;
}

/// Ближайший маркер, начиная с from; конец шаблона — если маркера нет.
///
/// Ищется "${}": он входит в оба маркера — сам по себе это плейсхолдер, а с
/// '$' перед ним экранирование, — поэтому всякая находка и есть маркер,
/// отсеивать ложные срабатывания не нужно и цикл не нужен тоже.
///
/// Отступ на байт назад обязателен: без него '$$${}' разобралось бы как
/// литерал '$$' плюс плейсхолдер, а не как литерал '$' плюс экранирование.
/// Граница pos > from не даёт заглянуть левее начала пробега — там стоит
/// либо конец предыдущего маркера, либо начало шаблона.
///
/// Раньше образцы сверялись на каждом байте, и это стоило ~4.2 нс на байт
/// шаблона — на длинном тексте почти всё время вычисления. Поиск подстроки
/// сводит цену к ~0.02 нс на байт. Плата — шаблон из одних '$', где быстрый
/// путь вырождается и выходит ~6.4 нс на байт; сложность остаётся линейной,
/// а такой шаблон бессмыслен, поэтому размен принят сознательно.
std::size_t findMarker(std::string_view fmt, std::size_t from) noexcept {
    const std::size_t pos = fmt.find("${}", from);
    if (pos == std::string_view::npos) { return fmt.size(); }
    return (pos > from && fmt[pos - 1] == '$') ? pos - 1 : pos;
}

}  // namespace

bool nextFormatPiece(std::string_view fmt, FormatCursor &cursor,
                     FormatPiece *piece, std::string_view *text) noexcept {
    if (cursor.pos >= fmt.size()) { return false; }

    if (const std::size_t length = markerAt(fmt, cursor.pos); length != 0) {
        *piece = length == 4 ? FormatPiece::Escaped : FormatPiece::Placeholder;
        *text = fmt.substr(cursor.pos, length);
        cursor.pos += length;
        return true;
    }

    // Литеральный пробег тянется до ближайшего маркера либо до конца шаблона.
    // Продвижение курсора гарантировано: на cursor.pos маркера нет — это
    // только что проверено, — значит findMarker вернёт строго большую позицию.
    const std::size_t start = cursor.pos;
    cursor.pos = findMarker(fmt, start);
    assert(cursor.pos > start && "литеральный пробег обязан продвигать курсор");
    *piece = FormatPiece::Literal;
    *text = fmt.substr(start, cursor.pos - start);
    return true;
}

std::uint32_t countPlaceholders(std::string_view fmt) noexcept {
    std::uint32_t count = 0;
    FormatCursor cursor;
    FormatPiece piece;
    std::string_view text;
    while (nextFormatPiece(fmt, cursor, &piece, &text)) {
        if (piece == FormatPiece::Placeholder) { ++count; }
    }
    return count;
}

namespace {

bool failType(std::uint32_t offset, const char *message, Diagnostic &diag) {
    diag = Diagnostic{ErrorCode::Type, offset, message};
    return false;
}

}  // namespace

namespace {

/// Все четыре арифметические функции требуют Number и отказывают одинаково.
bool numbersOnly(const Value *args, std::uint32_t count, const char *what,
                 std::uint32_t offset, Diagnostic &diag) {
    for (std::uint32_t i = 0; i < count; ++i) {
        if (args[i].kind() != Value::Kind::Number) {
            return failType(offset, what, diag);
        }
    }
    return true;
}

/// Выбор из двух чисел с распространением NaN.
///
/// std::fmin и std::fmax по стандарту NaN игнорируют и возвращают второй
/// операнд. В этом языке так не годится: арифметика NaN распространяет (§5.2),
/// сравнения его не прячут (§5.3), и min с max — единственные, кто мог бы
/// сделать иначе. Тихо исчезающее NaN означало бы, что испорченное число из
/// данных доедет до экрана обычным.
double chooseOrNaN(double a, double b, bool takeSmaller) noexcept {
    if (std::isnan(a)) { return a; }
    if (std::isnan(b)) { return b; }
    return takeSmaller ? std::fmin(a, b) : std::fmax(a, b);
}

}  // namespace

bool coerceScalarToString(const Value &v, char *numberBuffer,
                          std::string_view *out, std::uint32_t offset,
                          Diagnostic &diag) {
    switch (v.kind()) {
        case Value::Kind::String: *out = stringBytes(v); return true;
        case Value::Kind::Number:
            *out = formatNumber(v.numberValue(), numberBuffer, kNumberBufferSize);
            return true;
        case Value::Kind::Boolean:
            *out = v.booleanValue() ? "true" : "false";
            return true;
        case Value::Kind::Null: *out = "null"; return true;
        default:
            // Одно сообщение на все позиции, требующие String (§4): ключ
            // объекта, str, format и приведение в обходе — это одно и то же
            // нарушение, а не частный случай ключа.
            return failType(offset, "aggregates cannot be converted to string",
                            diag);
    }
}

bool applyBuiltin(Builtin id, Execution &exec, const Value *args,
                  std::uint32_t count, std::uint32_t offset, Value *out,
                  Diagnostic &diag) {
    (void)count;  // арность гарантирована проходом

    switch (id) {
        case Builtin::Count:
            // Array, Object либо String (§8.1); у строки — байты, не символы.
            switch (args[0].kind()) {
                case Value::Kind::Array:
                    *out = Value::number(CS::arrayCount(args[0]));
                    return true;
                case Value::Kind::Object:
                    *out = Value::number(CS::objectCount(args[0]));
                    return true;
                case Value::Kind::String:
                    *out = Value::number(
                        static_cast<double>(stringBytes(args[0]).size()));
                    return true;
                default:
                    return failType(offset,
                                    "count expects an array, object or string",
                                    diag);
            }

        case Builtin::Keys: {
            if (args[0].kind() != Value::Kind::Object) {
                return failType(offset, "keys expects an object", diag);
            }
            const std::uint32_t size = CS::objectCount(args[0]);
            // Точное выделение: длина известна заранее.
            Value result = CS::makeArray(size, exec.clock(), exec.deferred());
            for (std::uint32_t i = 0; i < size; ++i) {
                // Порядок наружу не обещан (§8.2); мы отдаём тот, в котором
                // ключи лежат, и обещанием это не становится.
                arrayPush(result, CS::materialize(CS::objectKeyAt(args[0], i), exec.deferred()),
                         exec.clock());
            }
            *out = result;
            return true;
        }

        case Builtin::Has: {
            if (args[0].kind() != Value::Kind::Object) {
                return failType(offset, "has expects an object", diag);
            }
            char buffer[kNumberBufferSize];
            std::string_view key;
            if (!coerceScalarToString(args[1], buffer, &key, offset, diag)) {
                return false;
            }
            *out = Value::boolean(CS::objectHas(args[0], key));
            return true;
        }

        case Builtin::Last: {
            if (args[0].kind() != Value::Kind::Array) {
                return failType(offset, "last expects an array", diag);
            }
            const std::uint32_t size = CS::arrayCount(args[0]);
            // На пустом — null (§8.4): через индексацию это невыразимо.
            *out = size == 0 ? Value::null() : CS::arrayAt(args[0], size - 1);
            return true;
        }

        case Builtin::Push:
            if (args[0].kind() != Value::Kind::Array) {
                return failType(offset, "push expects an array", diag);
            }
            // Void: *out не трогается (§2.2).
            arrayPush(args[0], args[1], exec.clock());
            return true;

        case Builtin::Pop:
            if (args[0].kind() != Value::Kind::Array) {
                return failType(offset, "pop expects an array", diag);
            }
            // На пустом ничего не делает и не отказывает (§8.6). Снятое
            // значение никуда не идёт: pop его не возвращает.
            arrayPop(args[0], nullptr, exec.clock(), exec.deferred());
            return true;

        case Builtin::Str: {
            if (args[0].kind() == Value::Kind::String) {
                *out = args[0];
                return true;
            }
            char buffer[kNumberBufferSize];
            std::string_view text;
            // Агрегат отвергается тем же правилом §4, что и всюду: сообщение
            // общее, частных формулировок под каждый билтин не заводим.
            if (!coerceScalarToString(args[0], buffer, &text, offset, diag)) {
                return false;
            }
            // materialize, not an arena offset: the result of str is an
            // ordinary value and its caller may do anything with it —
            // including putting it into an aggregate or a global variable.
            *out = CS::materialize(text, exec.deferred());
            return true;
        }

        case Builtin::Min:
            if (!numbersOnly(args, 2, "min expects numbers", offset, diag)) {
                return false;
            }
            *out = Value::number(chooseOrNaN(args[0].numberValue(),
                                              args[1].numberValue(), true));
            return true;

        case Builtin::Max:
            if (!numbersOnly(args, 2, "max expects numbers", offset, diag)) {
                return false;
            }
            *out = Value::number(chooseOrNaN(args[0].numberValue(),
                                              args[1].numberValue(), false));
            return true;

        case Builtin::Abs:
            if (!numbersOnly(args, 1, "abs expects a number", offset, diag)) {
                return false;
            }
            *out = Value::number(std::fabs(args[0].numberValue()));
            return true;

        case Builtin::Round:
            if (!numbersOnly(args, 1, "round expects a number", offset, diag)) {
                return false;
            }
            // От нуля, а не к чётному (§8.10): ровно то, что делает std::round.
            *out = Value::number(std::round(args[0].numberValue()));
            return true;

        case Builtin::Format:
            // Сюда не приходят: format вариадичен, и буфер под заранее
            // вычисленные аргументы потребовал бы верхней границы их числа,
            // которой §8.8 не устанавливает. Поэтому его цикл живёт прямо в
            // вычислителе (core/src/eval.cpp, evalFormat) и applyBuiltin не
            // достигает.
            assert(false && "format обрабатывается вычислителем, не applyBuiltin");
            return failType(offset, "format is not handled here", diag);
    }
}

}  // namespace CS
