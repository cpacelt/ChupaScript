#include "report.hpp"

#include <ostream>

namespace chupa {

namespace {

/// Длина символа UTF-8 по его начальному байту.
///
/// 0xxxxxxx — один байт; 110xxxxx, 1110xxxx, 11110xxx — два, три, четыре.
/// Продолжающий байт (10xxxxxx) сам по себе символ не начинает; на такой
/// длина 1 — заведомо некорректный исходник этой веткой не зацикливается.
std::size_t utf8LeadLength(unsigned char byte) noexcept {
    if ((byte & 0x80u) == 0x00u) return 1;
    if ((byte & 0xE0u) == 0xC0u) return 2;
    if ((byte & 0xF0u) == 0xE0u) return 3;
    if ((byte & 0xF8u) == 0xF0u) return 4;
    return 1;
}

}  // namespace

std::uint32_t columnOf(std::string_view source, std::uint32_t offset) noexcept {
    const std::size_t limit =
        offset < source.size() ? offset : source.size();
    std::uint32_t column = 0;
    std::size_t i = 0;
    while (i < limit) {
        const std::size_t charLen =
            utf8LeadLength(static_cast<unsigned char>(source[i]));
        // Символ считается, только если он целиком укладывается до limit —
        // иначе offset указывает внутрь него, и колонку нужно округлить вниз,
        // не засчитывая начатый, но не завершённый символ.
        if (i + charLen > limit) {
            break;
        }
        i += charLen;
        ++column;
    }
    return column;
}

std::string caretLine(std::uint32_t column, std::uint32_t indent) {
    std::string line(static_cast<std::size_t>(indent) + column, ' ');
    line += '^';
    return line;
}

void reportDiagnostic(std::ostream &out, std::string_view source,
                      std::uint32_t indent, const CS::Diagnostic &diag) {
    out << caretLine(columnOf(source, diag.offset), indent) << "\n"
        << "error: " << diag.message << "\n";
}

}  // namespace chupa
