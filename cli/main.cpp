// Интерактивная оболочка языка. Разбор аргументов, цикл чтения, команды.
//
// Чистые части — печать значения и пересчёт колонки — живут в printer.* и
// report.*: они проверяются тестами, а этот файл читает поток и пишет в поток,
// поэтому проверяется прогоном через канал.
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "chupascript/chupascript.h"
#include "report.hpp"
#include "store.hpp"

#include "data.hpp"
#include "diagnostic.hpp"
#include "expression.hpp"
#include "printer.hpp"
#include "script.hpp"
#include "value.hpp"

namespace {

constexpr std::string_view kExprPrefix = "expr:";
constexpr std::string_view kScriptPrefix = "script:";

/// Что делать после разобранной строки.
enum class After { Continue, Reset, Quit };

/// Ширина приглашения: на неё сдвинута всякая колонка, потому что исходник
/// человек набирал после него.
constexpr std::uint32_t kPromptWidth = 2;

/// Сколько ошибок статического прохода помещается в один отчёт.
///
/// Проход возвращает, сколько нашлось всего, поэтому о непоместившихся есть что
/// сказать. Строка в оболочке короткая, и восьми хватает с запасом.
constexpr std::uint32_t kMaxReported = 8;

/// Общая часть выражения и скрипта: печатает находки компиляции.
///
/// Возвращает true, если компиляция удалась (errors == 0). Печатаются все
/// поместившиеся находки, а не первая: проход отдаёт их массивом именно
/// затем; о непоместившихся — отдельная строка.
bool reportCompile(std::uint32_t errors, std::string_view source,
                   std::uint32_t indent, const CS::Diagnostic *found,
                   std::uint32_t capacity) {
    if (errors == 0) { return true; }
    const std::uint32_t shown = errors < capacity ? errors : capacity;
    for (std::uint32_t i = 0; i < shown; ++i) {
        chupa::reportDiagnostic(std::cout, source, indent, found[i]);
    }
    if (errors > shown) {
        std::cout << "error: " << (errors - shown) << " more not shown\n";
    }
    return false;
}

/// Компилирует и вычисляет выражение.
///
/// source — то, что осталось после префикса режима; indent — ширина всего, что
/// напечатано до него.
void runExpression(CS::Store &store, CS::Execution &exec,
                   std::string_view source,
                   std::uint32_t indent) {
    CS::Diagnostic found[kMaxReported];
    CS::Expression expr;
    const std::uint32_t errors =
        CS::Expression::compile(source, store, &expr, found, kMaxReported);
    if (!reportCompile(errors, source, indent, found, kMaxReported)) { return; }

    // Сырой путь, а не evalString: оболочка печатает null наравне со всем
    // прочим и умеет агрегаты (cli/printer.cpp) — трёхзначный исход ей только
    // мешал бы.
    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    if (!expr.eval(store, exec, &out, diag)) {
        chupa::reportDiagnostic(std::cout, source, indent, diag);
        return;
    }
    std::cout << chupa::printValue(store, out) << "\n";
}

/// Компилирует и исполняет скрипт.
///
/// source — то, что осталось после префикса режима; indent — ширина всего, что
/// напечатано до него.
void runScriptSource(CS::Store &store, CS::Execution &exec,
                     std::string_view source, std::uint32_t indent) {
    CS::Diagnostic found[kMaxReported];
    CS::Script script;
    const std::uint32_t errors =
        CS::Script::compile(source, store, &script, found, kMaxReported);
    if (!reportCompile(errors, source, indent, found, kMaxReported)) { return; }

    // Скрипт при успехе молчит: значения у него нет, а результат виден через
    // :vars.
    CS::Diagnostic diag;
    if (!script.run(store, exec, diag)) {
        chupa::reportDiagnostic(std::cout, source, indent, diag);
    }
}

void printUsage(std::ostream &out) {
    out << "chupa " << chupa_version() << "\n"
        << "\n"
        << "usage:\n"
        << "  chupa -repl    start the interactive shell\n";
}

void printHelp(std::ostream &out) {
    out << "  expr: <expression>   evaluate an expression\n"
        << "  script: <statements> run a script\n"
        << "  :set <name> = <literal>  put a variable into the context\n"
        << "  :vars                    list the context\n"
        << "  :reset                   start with an empty context\n"
        << "  :help                    this text\n"
        << "  :quit                    leave\n";
}

/// Обрезает пробелы с обоих концов.
///
/// `\r` снимается наравне с пробелом и табуляцией: `std::getline` режет только
/// по `\n`, поэтому файл, набранный на Windows (`\r\n`), оставляет `\r`
/// последним символом каждой строки. Без этого `:quit\r` не совпадает с
/// `"quit"`, и оболочка не выходит, ожидая конца ввода.
std::string_view trim(std::string_view text) {
    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    std::size_t first = 0;
    while (first < text.size() && isSpace(text[first])) { ++first; }
    std::size_t last = text.size();
    while (last > first && isSpace(text[last - 1])) { --last; }
    return text.substr(first, last - first);
}

/// Кладёт переменную: та же дверь, которой пользуется хост.
///
/// setVariable принимает только литерал — данные не вычисляются
/// (docs/superpowers/specs/2026-08-11-chupascript-data-design.md §3). В
/// оболочке это ограничение встречается первым, поэтому отказ поясняется.
void runSet(CS::Store &store, std::string_view argument) {
    const std::size_t equals = argument.find('=');
    if (equals == std::string_view::npos) {
        std::cout << "error: usage is :set <name> = <literal>\n";
        return;
    }
    const std::string_view name = trim(argument.substr(0, equals));
    const std::string_view text = trim(argument.substr(equals + 1));
    if (name.empty() || text.empty()) {
        std::cout << "error: usage is :set <name> = <literal>\n";
        return;
    }

    CS::Diagnostic diag;
    if (CS::setVariable(store, name, text, diag)) { return; }

    std::cout << "error: " << diag.message << "\n";
    if (diag.code == CS::ErrorCode::Data) {
        std::cout << "note: data is set from a literal, not an expression\n";
    }
}

/// Печатает состав хранилища: имя и значение.
void runVars(const CS::Store &store) {
    const std::uint32_t count = store.globalCount();
    if (count == 0) {
        std::cout << "the context is empty\n";
        return;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::string_view name = store.globalNameAt(i);
        std::cout << name << " = "
                  << chupa::printValue(store, store.global(name)) << "\n";
    }
}

/// Выполняет одну строку.
After handleLine(CS::Store &store, CS::Execution &exec, std::string_view line) {
    const std::string_view text = trim(line);
    if (text.empty()) { return After::Continue; }

    if (text[0] == ':') {
        const std::string_view body = trim(text.substr(1));
        const std::size_t space = body.find_first_of(" \t");
        const std::string_view name =
            space == std::string_view::npos ? body : body.substr(0, space);
        const std::string_view argument =
            space == std::string_view::npos ? std::string_view()
                                            : trim(body.substr(space));

        if (name == "quit") { return After::Quit; }
        if (name == "help") {
            printHelp(std::cout);
            return After::Continue;
        }
        if (name == "vars") {
            runVars(store);
            return After::Continue;
        }
        if (name == "set") {
            runSet(store, argument);
            return After::Continue;
        }
        if (name == "reset") {
            // Хранилище копит мусор — освобождения по одному нет
            // (docs/backlog.md B1). :reset единственный способ начать чисто,
            // не выходя из оболочки.
            return After::Reset;   // сам сброс делает вызывающий, см. runRepl
        }
        std::cout << "error: unknown command\n";
        printHelp(std::cout);
        return After::Continue;
    }

    const bool isExpr = text.substr(0, kExprPrefix.size()) == kExprPrefix;
    const bool isScript = text.substr(0, kScriptPrefix.size()) == kScriptPrefix;
    if (isExpr || isScript) {
        const std::size_t prefixSize =
            isExpr ? kExprPrefix.size() : kScriptPrefix.size();
        const std::string_view rest = text.substr(prefixSize);
        const std::string_view source = trim(rest);

        // Отступ считается по исходной строке, а не по обрезанной: человек
        // видит на экране приглашение, префикс и те пробелы, что набрал сам.
        const std::size_t sourceStart =
            static_cast<std::size_t>(source.data() - line.data());
        const std::uint32_t indent =
            kPromptWidth + chupa::columnOf(line, static_cast<std::uint32_t>(
                                                     sourceStart));

        if (isScript) {
            runScriptSource(store, exec, source, indent);
        } else {
            runExpression(store, exec, source, indent);
        }
        return After::Continue;
    }

    // Режим задаётся человеком: оболочка не угадывает, выражение это или
    // скрипт (docs/superpowers/specs/2026-08-13-chupa-repl-design.md §3).
    std::cout << "error: a line must start with 'expr:', 'script:' or ':'\n";
    return After::Continue;
}

/// Инвариант времени жизни: ни один `Value`, ни один срез (`string_view`),
/// полученный из `store`, не переживает вызов `handleLine`, который его
/// произвёл. Ядро этой дисциплины ничем не подпирает — `Value` это индекс в
/// пулы хранилища, а `:reset` пересоздаёт `store` целиком (`store.emplace()`
/// ниже), так что значение, пережившее сброс, стало бы индексом в чужой,
/// вновь построенное хранилище и молча указывало бы не на то, чем было. Это
/// свойство, которое обязана сохранять всякая правка этого цикла, а не
/// исторический факт о нём: команда вроде гипотетической `:last`, кладущей
/// последнее значение про запас между строками, его нарушит.
int runRepl() {
    std::optional<CS::Store> store;
    store.emplace();

    // Состояние выполнения пересоздаётся вместе с хранилищем: временный
    // регион — такое же хранилище, и переживший сброс Value указывал бы в
    // чужие пулы.
    std::optional<CS::Execution> exec;
    exec.emplace();

    std::cout << "chupa " << chupa_version() << ", :help for commands\n";

    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            // Конец ввода: перевод строки, чтобы приглашение не осталось
            // висеть, и выход.
            std::cout << "\n";
            break;
        }
        const After after = handleLine(*store, *exec, line);
        if (after == After::Quit) { break; }
        if (after == After::Reset) {
            store.emplace();
            exec.emplace();
            std::cout << "the context is empty\n";
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "-repl") {
        return runRepl();
    }
    if (argc == 1) {
        printUsage(std::cout);
        return 0;
    }
    printUsage(std::cerr);
    return 2;
}
