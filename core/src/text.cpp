#include "text.hpp"

#include <cassert>
#include <charconv>
#include <cmath>
#include <system_error>

namespace CS {

std::string_view decodeEscapes(std::string_view raw, std::string &scratch) {
    scratch.clear();
    scratch.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\') {
            scratch.push_back(raw[i]);
            continue;
        }
        assert(i + 1 < raw.size() && "лексер не пропустил бы висячий слэш");
        ++i;
        switch (raw[i]) {
            case 'n': scratch.push_back('\n'); break;
            case 't': scratch.push_back('\t'); break;
            case '\\': scratch.push_back('\\'); break;
            case '\'': scratch.push_back('\''); break;
            case '"': scratch.push_back('"'); break;
            default: assert(false && "лексер отверг бы такую последовательность");
        }
    }
    return scratch;
}

std::string_view literalText(const Ast &ast, NodeId node, std::string &scratch) {
    assert(ast.kind(node) == NodeKind::String);
    if (!ast.hasEscape(node)) { return ast.text(node); }
    return decodeEscapes(ast.text(node), scratch);
}

std::string_view formatNumber(double value, char *buffer, std::size_t size) {
    assert(size >= kNumberBufferSize);

    if (std::isnan(value)) { return "nan"; }
    if (std::isinf(value)) { return value > 0.0 ? "inf" : "-inf"; }
    // Ноль отдельно: общее правило отправило бы его в научную запись, где он
    // выглядит как 0e+00.
    if (value == 0.0) { return std::signbit(value) ? "-0" : "0"; }

    // Порог выбран так, чтобы 1000000 осталось 1000000, а 1e21 стало 1e+21 —
    // ровно как в таблице примеров docs/semantics.md §4.3.
    const double magnitude = std::fabs(value);
    const std::chars_format format = (magnitude >= 1e-7 && magnitude < 1e21)
                                         ? std::chars_format::fixed
                                         : std::chars_format::scientific;

    const std::to_chars_result result =
        std::to_chars(buffer, buffer + size, value, format);
    assert(result.ec == std::errc() && "kNumberBufferSize оказался мал");
    return std::string_view(buffer,
                            static_cast<std::size_t>(result.ptr - buffer));
}

}  // namespace CS
