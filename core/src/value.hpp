#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace CS {

class Store;

namespace detail {
/// Коробки со счётчиком ссылок (core/src/box.hpp). Здесь только объявления:
/// значение держит указатель, а раскладку коробки ему знать незачем.
struct Box;
struct StringBox;
struct ArrayBox;
struct ObjectBox;
}  // namespace detail

/// Номер ячейки значения глобальной переменной в хранилище.
///
/// Живёт здесь, а не в store.hpp, потому что нужен обеим сторонам: хранилище
/// номера выдаёт, дерево разбора их запоминает (Ast::globalValuesSlot), а
/// зависимость ast.hpp → store.hpp завела бы синтаксис в знание о значениях.
///
/// Номер годен, пока живо выдавшее его хранилище: ячейки только добавляются в
/// конец и никогда не переезжают и не переиспользуются — удаления глобальных
/// переменных в языке нет. Поэтому номер, разрешённый однажды на компиляции,
/// остаётся верным навсегда, сколько бы имён ни завели после.
using GlobalSlot = std::uint32_t;

/// Имени нет. Значением ячейки быть не может: столько их не бывает.
inline constexpr GlobalSlot kNoGlobalSlot = 0xffffffffu;

/// A ChupaScript value: sixteen bytes, trivially copyable, self-contained.
///
///         0        1                                              15
///       ┌────┬────────────────────────────────────────────────────┐
/// Inline│ tag│ b0 b1 b2 b3 b4 b5 b6 b7 b8 b9 b10 b11 b12 b13 b14  │  string <= 15
///       └────┴────────────────────────────────────────────────────┘
///       ┌────┬───────────────────┬───────────────────────────────┐
/// Number│ tag│      padding      │            double             │
///       └────┴───────────────────┴───────────────────────────────┘
///       ┌────┬───────────────────┬───────────────────────────────┐
/// Boxed │ tag│      padding      │           Box *               │  long string,
///       └────┴───────────────────┴───────────────────────────────┘  array, object
///       ┌────┬─┐
/// Bool  │ tag│b│                                    Null: the tag alone
///       └────┴─┘
///
/// The tag byte:
///
///    bit  7   6 5 4 3   2 1 0
///         │   └───┬───┘ └─┬─┘
///         │       │       └── kind: Null 0, Boolean 1, Number 2,
///         │       │            String 3, Object 4, Array 5
///         │       └────────── inline string length, 0..15; meaningful only
///         │                    when the kind is String
///         └────────────────── string is inline (1) or boxed (0); meaningful
///                              only when the kind is String
///
/// Self-contained is the one rule of the memory model: a value can be read
/// anywhere, at any time, including after the Context that produced it has
/// been destroyed. A short string carries its bytes; a long string and an
/// aggregate carry a pointer to a reference-counted box, and the box carries
/// its bytes and — for an object — its own field-name table.
///
/// INVARIANT: in an inline string, the bytes past the length are zero. That is
/// what makes comparing two inline strings a comparison of two eight-byte
/// words, with no length to consult and no memcmp over a variable range.
///
/// NaN-boxing was rejected permanently (design document Р2): eight bytes are
/// spent entirely on the double and the tags, leaving no room for string
/// bytes, and the BDUI measurements name strings as the only place the engine
/// loses.
class Value {
   public:
    enum class Kind : std::uint8_t {
        Null = 0, Boolean = 1, Number = 2, String = 3, Object = 4, Array = 5
    };

    /// The longest string that fits inside a value: sixteen bytes minus the
    /// tag.
    static constexpr std::size_t kInlineCapacity = 15;

    [[nodiscard]] static Value null() noexcept { return Value{}; }

    [[nodiscard]] static Value boolean(bool value) noexcept {
        Value v;
        v.wide_.tag = tagOf(Kind::Boolean);
        v.wide_.flag = value;
        return v;
    }

    [[nodiscard]] static Value number(double value) noexcept {
        Value v;
        v.wide_.tag = tagOf(Kind::Number);
        v.wide_.number = value;
        return v;
    }

    /// A string short enough to live inside the value.
    /// Precondition: bytes.size() <= kInlineCapacity.
    [[nodiscard]] static Value inlineString(std::string_view bytes) noexcept {
        assert(bytes.size() <= kInlineCapacity);
        // The default constructor value-initialises wide_, which zeroes all
        // sixteen bytes including the padding — so every byte past the length
        // is already zero, which is the invariant the equality fast path
        // stands on.
        Value v;
        v.inline_.tag = static_cast<std::uint8_t>(
            tagOf(Kind::String) |
            (static_cast<std::uint8_t>(bytes.size()) << kLengthShift) |
            kInlineFlag);
        if (!bytes.empty()) {
            std::memcpy(v.inline_.bytes, bytes.data(), bytes.size());
        }
        return v;
    }

    /// A string too long to live inside the value. The box carries its own
    /// length; the tag's length field stays zero and is never read for a boxed
    /// string.
    [[nodiscard]] static Value string(detail::StringBox *box) noexcept {
        Value v;
        v.wide_.tag = tagOf(Kind::String);  // inline flag left clear
        v.wide_.box = reinterpret_cast<detail::Box *>(box);
        return v;
    }

    [[nodiscard]] static Value array(detail::ArrayBox *box) noexcept {
        Value v;
        v.wide_.tag = tagOf(Kind::Array);
        v.wide_.box = reinterpret_cast<detail::Box *>(box);
        return v;
    }

    [[nodiscard]] static Value object(detail::ObjectBox *box) noexcept {
        Value v;
        v.wide_.tag = tagOf(Kind::Object);
        v.wide_.box = reinterpret_cast<detail::Box *>(box);
        return v;
    }

    /// The box this value references.
    /// Precondition: referencesBox().
    [[nodiscard]] detail::Box *box() const noexcept {
        assert(referencesBox());
        return wide_.box;
    }

    /// Precondition: kind() == Kind::Boolean.
    [[nodiscard]] bool booleanValue() const noexcept {
        assert(kind() == Kind::Boolean);
        return wide_.flag;
    }

    /// Precondition: kind() == Kind::Number.
    [[nodiscard]] double numberValue() const noexcept {
        assert(kind() == Kind::Number);
        return wide_.number;
    }

    /// Are these two the same aggregate — same kind, same box.
    ///
    /// Scalars have no identity (docs/semantics.md §5.4), so this is false for
    /// them even when a value is compared with itself.
    [[nodiscard]] bool sameAggregate(const Value &other) const noexcept {
        const Kind k = kind();
        if (k != other.kind()) { return false; }
        if (k != Kind::Array && k != Kind::Object) { return false; }
        return wide_.box == other.wide_.box;
    }

    [[nodiscard]] Kind kind() const noexcept {
        return static_cast<Kind>(wide_.tag & kKindMask);
    }

    /// Precondition: kind() == Kind::String.
    [[nodiscard]] bool isInlineString() const noexcept {
        assert(kind() == Kind::String);
        return (wide_.tag & kInlineFlag) != 0;
    }

    /// The bytes of an inline string. Points INTO this value, so it lives
    /// exactly as long as this value does — read it through stringBytes,
    /// which says so at every call site.
    /// Precondition: kind() == Kind::String && isInlineString().
    [[nodiscard]] std::string_view inlineBytes() const noexcept {
        assert(kind() == Kind::String && isInlineString());
        return std::string_view(inline_.bytes, inlineLength());
    }

    /// Does this value own a reference to a box.
    [[nodiscard]] bool referencesBox() const noexcept {
        const Kind k = kind();
        if (k == Kind::Object || k == Kind::Array) { return true; }
        return k == Kind::String && (wide_.tag & kInlineFlag) == 0;
    }

   private:
    static constexpr std::uint8_t kKindMask = 0x07;    // bits 0-2
    static constexpr std::uint8_t kLengthShift = 3;    // bits 3-6
    static constexpr std::uint8_t kLengthMask = 0x78;
    static constexpr std::uint8_t kInlineFlag = 0x80;  // bit 7

    static constexpr std::uint8_t tagOf(Kind kind) noexcept {
        return static_cast<std::uint8_t>(kind);
    }

    [[nodiscard]] std::size_t inlineLength() const noexcept {
        return static_cast<std::size_t>((wide_.tag & kLengthMask) >> kLengthShift);
    }

    /// Both layouts are standard-layout and share the tag as their common
    /// initial sequence, so the tag may be read through either member whichever
    /// one is active ([class.mem]). Nothing else may.
    struct Inline { std::uint8_t tag; char bytes[15]; };
    struct Wide {
        std::uint8_t tag;
        std::uint8_t pad[7];
        union { double number; bool flag; detail::Box *box; };
    };

    Value() noexcept : wide_{} {}  // tag 0 == Kind::Null

    union { Inline inline_; Wide wide_; };
};

static_assert(sizeof(Value) == 16, "Value must stay sixteen bytes");
static_assert(alignof(Value) == 8, "the double in the payload sets the alignment");
static_assert(std::is_trivially_copyable_v<Value>,
              "values are copied in bulk inside aggregates");

}  // namespace CS
