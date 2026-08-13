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
#include "context.hpp"
#include "report.hpp"

namespace {

constexpr std::string_view kExprPrefix = "expr:";
constexpr std::string_view kScriptPrefix = "script:";

/// Что делать после разобранной строки.
enum class After { Continue, Quit };

void printUsage(std::ostream &out) {
    out << "chupa " << chupascript_version() << "\n"
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
std::string_view trim(std::string_view text) {
    std::size_t first = 0;
    while (first < text.size() && (text[first] == ' ' || text[first] == '\t')) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t')) {
        --last;
    }
    return text.substr(first, last - first);
}

/// Выполняет одну строку.
After handleLine(CS::Context &ctx, std::string_view line) {
    (void)ctx;
    const std::string_view text = trim(line);
    if (text.empty()) { return After::Continue; }

    if (text[0] == ':') {
        const std::string_view command = trim(text.substr(1));
        if (command == "quit") { return After::Quit; }
        if (command == "help") {
            printHelp(std::cout);
            return After::Continue;
        }
        std::cout << "error: unknown command\n";
        printHelp(std::cout);
        return After::Continue;
    }

    if (text.substr(0, kExprPrefix.size()) == kExprPrefix ||
        text.substr(0, kScriptPrefix.size()) == kScriptPrefix) {
        std::cout << "error: not implemented yet\n";
        return After::Continue;
    }

    // Режим задаётся человеком: оболочка не угадывает, выражение это или
    // скрипт (docs/superpowers/specs/2026-08-13-chupa-repl-design.md §3).
    std::cout << "error: a line must start with 'expr:', 'script:' or ':'\n";
    return After::Continue;
}

int runRepl() {
    std::optional<CS::Context> ctx;
    ctx.emplace();

    std::cout << "chupa " << chupascript_version() << ", :help for commands\n";

    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            // Конец ввода: перевод строки, чтобы приглашение не осталось
            // висеть, и выход.
            std::cout << "\n";
            break;
        }
        if (handleLine(*ctx, line) == After::Quit) { break; }
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
