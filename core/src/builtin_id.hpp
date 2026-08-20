#pragma once
#include <cstdint>

namespace CS {

/// Встроенные функции языка (docs/semantics.md §8).
///
/// Порядок совпадает с алфавитным порядком имён: таблица в builtin.cpp ищется
/// двоично, и индексация в builtinInfo стоит на этом совпадении.
///
/// Живёт здесь, а не в builtin.hpp, по той же причине, по какой GlobalSlot
/// живёт в value.hpp, а не в store.hpp: список нужен обеим сторонам. Проход
/// разрешает имя вызова в эту величину и кладёт её в узел (Ast::setCallee), а
/// builtin.hpp объявляет применение функций и потому тянет за собой хранилище —
/// зависимость ast.hpp → builtin.hpp завела бы синтаксис в знание о значениях.
///
/// Разделение проходит по смыслу, а не по удобству: какие имена вообще являются
/// функциями — факт уровня разбора, а что эти функции делают со значениями —
/// уровня вычисления.
enum class Builtin : std::uint8_t {
    Abs, Count, Format, Has, Keys, Last, Max, Min, Pop, Push, Round, Str
};

/// Имя не разрешено: узел Call до прохода, либо имя, которого в таблице нет.
///
/// Значением функции быть не может: столько встроенных функций не бывает, и
/// static_assert в builtin.cpp сторожит это, сверяя размер таблицы с enum.
inline constexpr Builtin kNoBuiltin = static_cast<Builtin>(255);

/// What the name of a Call node resolved to: a builtin, a host function, or
/// nothing yet.
///
/// LAYOUT — one byte, three ranges:
///
///   0 … 127     a builtin; the value is the Builtin itself
///   128 … 254   a host function; the index into the Context's table is
///               (value - 128), so 0 … 126
///   255         unresolved — kNoCallee
///
/// One byte because that is the room Ast::Node has: the field sits at offset
/// 2 of a node that must stay 24 bytes (ast.hpp). The rejected alternative
/// was a wider field in the four spare bytes at offset 20 — four bytes on
/// every node in the tree, paid to lift a ceiling of 127 host functions that
/// no host comes near.
///
/// Lives beside Builtin, and for the same reason stated above it: what a name
/// resolves to is a fact of parsing, and ast.hpp must learn it without
/// depending on builtin.hpp, which knows about values.
enum class CalleeRef : std::uint8_t {};

/// Bit that tells the two ranges apart.
inline constexpr std::uint8_t kHostCalleeBit = 0x80;

/// Name not resolved: a Call node before check, or a name no table holds.
inline constexpr CalleeRef kNoCallee = static_cast<CalleeRef>(255);

/// How many host functions one Context may hold. 127, not 128: index 127
/// would encode as 255, which is kNoCallee.
inline constexpr std::uint8_t kMaxHostFunctions = 127;

constexpr CalleeRef calleeOfBuiltin(Builtin id) noexcept {
    return static_cast<CalleeRef>(static_cast<std::uint8_t>(id));
}

constexpr CalleeRef calleeOfHost(std::uint8_t index) noexcept {
    return static_cast<CalleeRef>(index | kHostCalleeBit);
}

constexpr bool isHostCallee(CalleeRef ref) noexcept {
    return ref != kNoCallee &&
           (static_cast<std::uint8_t>(ref) & kHostCalleeBit) != 0;
}

/// Precondition: !isHostCallee(ref) and ref != kNoCallee.
constexpr Builtin builtinOfCallee(CalleeRef ref) noexcept {
    return static_cast<Builtin>(static_cast<std::uint8_t>(ref));
}

/// Precondition: isHostCallee(ref).
constexpr std::uint8_t hostIndexOfCallee(CalleeRef ref) noexcept {
    return static_cast<std::uint8_t>(ref) & ~kHostCalleeBit;
}

static_assert(static_cast<std::uint8_t>(Builtin::Str) < kHostCalleeBit,
              "the builtin table grew into the host range: widen CalleeRef");

}  // namespace CS
