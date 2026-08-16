#include "text.hpp"

#include <cassert>
#include <cmath>

#include "double-conversion/double-to-string.h"

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

std::string_view literalText(const Ast &ast, NodeId node,
                             std::string_view source, std::string &scratch) {
    assert(ast.kind(node) == NodeKind::String);
    const std::string_view raw = ast.text(node, source);
    return ast.hasEscape(node) ? decodeEscapes(raw, scratch) : raw;
}

namespace {

/// Конвертер, настроенный под docs/semantics.md §4.3.
///
/// Порог фиксированной записи задаётся парой границ показателя: позиционная
/// запись берётся на полуинтервале [10^-7, 10^21), научная — вне его. Это
/// ровно правило 3 спеки, только выраженное настройкой, а не нашим кодом.
///
/// EMIT_POSITIVE_EXPONENT_SIGN даёт `1e+21` вместо `1e21`. Показатель
/// печатается минимальным числом цифр, поэтому `1e-8` остаётся `1e-8` —
/// правило 4 спеки, совпадающее с JavaScript.
///
/// Символы бесконечности и NaN не задаются: эти значения обрабатываются до
/// вызова конвертера, и передавать ему nullptr безопаснее — если особое
/// значение всё же дойдёт сюда, ToShortest вернёт false, а не тихо напечатает
/// не тот текст.
const double_conversion::DoubleToStringConverter &numberFormatter() noexcept {
    using Converter = double_conversion::DoubleToStringConverter;
    static const Converter formatter(Converter::EMIT_POSITIVE_EXPONENT_SIGN,
                                     nullptr,  // бесконечность сюда не доходит
                                     nullptr,  // NaN сюда не доходит
                                     'e',
                                     -7,   // нижняя граница позиционной записи
                                     21,   // верхняя граница, не включая
                                     0, 0);  // добивка нулями не используется
    return formatter;
}

}  // namespace

std::string_view formatNumber(double value, char *buffer, std::size_t size) {
    static_assert(
        kNumberBufferSize >
            double_conversion::DoubleToStringConverter::kMaxCharsEcmaScriptShortest,
        "буфера не хватит на кратчайшую запись");
    assert(size >= kNumberBufferSize);

    if (std::isnan(value)) { return "nan"; }
    if (std::isinf(value)) { return value > 0.0 ? "inf" : "-inf"; }
    // Ноль отдельно: конвертер напечатал бы его как 0, потеряв знак, а
    // отрицательный ноль обязан давать '-0' — от этого зависят ключи объектов.
    if (value == 0.0) { return std::signbit(value) ? "-0" : "0"; }

    double_conversion::StringBuilder out(buffer, static_cast<int>(size));
    const bool written = numberFormatter().ToShortest(value, &out);
    assert(written && "особые значения отсеяны выше");
    (void)written;
    return std::string_view(buffer, static_cast<std::size_t>(out.position()));
}

}  // namespace CS
