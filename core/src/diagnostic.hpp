#pragma once
#include <cstdint>

namespace CS {

/// Класс ошибки.
///
/// Значения намеренно совпадают с ChupaErrorCode из публичного C-заголовка;
/// соответствие будет закреплено static_assert'ами, когда тот появится.
enum class ErrorCode : std::uint8_t {
    None = 0,
    Syntax,  ///< лексика и синтаксис
    Name,    ///< неизвестная глобальная переменная, функция, число аргументов
    Type,    ///< не тот тип операнда при выполнении
    Range,   ///< индекс массива, запись за границу
    Data,    ///< значение переменной не является литералом
    Usage,   ///< нарушён порядок вызовов
    Memory
};

/// Описание одной неудачи.
///
/// message — статическая строка; Diagnostic ничем не владеет и свободно
/// копируется.
struct Diagnostic {
    ErrorCode code = ErrorCode::None;
    std::uint32_t offset = 0;  ///< смещение в байтах от начала исходника
    const char *message = "";
};

}  // namespace CS
