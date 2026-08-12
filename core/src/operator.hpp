#pragma once
#include <cstdint>

#include "context.hpp"
#include "diagnostic.hpp"
#include "token.hpp"
#include "value.hpp"

namespace CS {

/// Применяет бинарную операцию без короткого замыкания.
///
/// op — одна из: Plus, Minus, Star, Slash, Percent, Less, Greater, LessEqual,
/// GreaterEqual, Equal, NotEqual. Логические, ?? и тернарный сюда не попадают:
/// они решают, вычислять ли операнд, а здесь оба уже вычислены.
///
/// ctx нужен единственной операции — сравнению строк на равенство. offset
/// попадает в diag при отказе: это данные, а не зависимость от дерева. При
/// отказе *out не трогается.
bool applyBinary(TokenKind op, Value lhs, Value rhs, const Context &ctx,
                 std::uint32_t offset, Value *out, Diagnostic &diag);

/// Применяет унарную операцию: Bang над Boolean, Minus над Number.
///
/// Контекст не нужен: ни одна из двух операций не заглядывает в хранилище.
/// При отказе *out не трогается.
bool applyUnary(TokenKind op, Value operand, std::uint32_t offset, Value *out,
                Diagnostic &diag);

}  // namespace CS
