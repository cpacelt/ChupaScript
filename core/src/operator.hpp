#pragma once
#include <cstdint>

#include "diagnostic.hpp"
#include "store.hpp"
#include "token.hpp"
#include "value.hpp"

namespace CS {

/// Применяет бинарную операцию без короткого замыкания.
///
/// op — одна из: Plus, Minus, Star, Slash, Percent, Less, Greater, LessEqual,
/// GreaterEqual, Equal, NotEqual. Логические, ?? и тернарный сюда не попадают:
/// они решают, вычислять ли операнд, а здесь оба уже вычислены.
///
/// Хранилища нужны единственной операции — сравнению строк на равенство, и их
/// два, потому что операнды вправе лежать в разных регионах: `format(...) ==
/// state.name` сравнивает временную строку с постоянной. Каждое хранилище
/// разрешает свой операнд и только его (docs/backlog.md [B57]).
///
/// offset попадает в diag при отказе: это данные, а не зависимость от дерева.
/// При отказе *out не трогается.
bool applyBinary(TokenKind op, Value lhs, Value rhs, const Store &lhsStore,
                 const Store &rhsStore, std::uint32_t offset, Value *out,
                 Diagnostic &diag);

/// Применяет унарную операцию: Bang над Boolean, Minus над Number.
///
/// Хранилище не нужно: ни одна из двух операций в него не заглядывает.
/// При отказе *out не трогается.
bool applyUnary(TokenKind op, Value operand, std::uint32_t offset, Value *out,
                Diagnostic &diag);

}  // namespace CS
