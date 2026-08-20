#pragma once
#include <cstdint>

namespace CS {

/// Класс ошибки.
///
/// Значения намеренно совпадают с ChupaErrorCode из публичного C-заголовка;
/// соответствие закреплено static_assert'ами в c_api.cpp.
enum class ErrorCode : std::uint8_t {
    None = 0,
    Syntax,  ///< лексика и синтаксис
    Name,    ///< неизвестная глобальная переменная, функция, число аргументов
    Type,    ///< не тот тип операнда при выполнении
    Range,   ///< индекс массива, запись за границу
    Data,    ///< значение переменной не является литералом
    Usage,   ///< нарушён порядок вызовов
    Memory,
    Host     ///< коллбэк хоста отказал, не назвав причину через chupa_fail
};

/// Описание одной неудачи.
///
/// message обычно статическая строка, и Diagnostic ничем не владеет и
/// свободно копируется. Исключение — путь отказа хоста: там message
/// указывает внутрь Execution::hostFailureText_ (execution.hpp) и остаётся
/// годным лишь до следующего chupa_fail на этом контексте, тот же срок,
/// что публичный заголовок обещает для ChupaError.message.
struct Diagnostic {
    ErrorCode code = ErrorCode::None;
    std::uint32_t offset = 0;  ///< смещение в байтах от начала исходника
    const char *message = "";
};

}  // namespace CS
