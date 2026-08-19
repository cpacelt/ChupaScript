#pragma once
#include <cassert>
#include <cstdint>
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

/// A ChupaScript value — one of the six kinds in docs/semantics.md §2.1.
///
/// A String, Object or Array is a pointer to a reference-counted box
/// (detail::Box and its subtypes in box.hpp): such a value is self-contained
/// and reads without any Store at all. There is no other way to address a
/// string, object or array any more — the arena-offset encoding this class
/// used to carry for temporary strings is gone; every string is a box from
/// the moment it exists.
///
/// Layout and rationale:
/// docs/superpowers/specs/2026-08-11-chupascript-values-design.md §4.
class Value {
   public:
    /// Вид значения. Закрытый список из docs/semantics.md §2.1.
    enum class Kind : std::uint8_t { Null, Boolean, Number, String, Object, Array };

    [[nodiscard]] static Value null() noexcept {
        Value v;
        v.kind_ = Kind::Null;
        return v;
    }

    [[nodiscard]] static Value boolean(bool value) noexcept {
        Value v;
        v.kind_ = Kind::Boolean;
        v.boolean_ = value;
        return v;
    }

    [[nodiscard]] static Value number(double value) noexcept {
        Value v;
        v.kind_ = Kind::Number;
        v.number_ = value;
        return v;
    }

    [[nodiscard]] Kind kind() const noexcept { return kind_; }

    /// Коробка, на которую значение ссылается.
    /// Предусловие: referencesBox().
    [[nodiscard]] detail::Box *box() const noexcept {
        assert(referencesBox());
        return box_;
    }

    /// Ссылается ли значение на коробку. У скаляров коробки нет: копия числа
    /// ни с какой коробкой не связана.
    [[nodiscard]] bool referencesBox() const noexcept {
        return kind_ == Kind::String || kind_ == Kind::Object || kind_ == Kind::Array;
    }

    /// Предусловие: kind() == Kind::Boolean.
    [[nodiscard]] bool booleanValue() const noexcept {
        assert(kind_ == Kind::Boolean);
        return boolean_;
    }

    /// Предусловие: kind() == Kind::Number.
    [[nodiscard]] double numberValue() const noexcept {
        assert(kind_ == Kind::Number);
        return number_;
    }

    /// Один ли это агрегат — сравнивает вид и адрес коробки.
    ///
    /// У скаляров идентичности нет (docs/semantics.md §5.4), для них всегда
    /// false, в том числе при сравнении значения с самим собой.
    [[nodiscard]] bool sameAggregate(Value other) const noexcept {
        if (kind_ != other.kind_) { return false; }
        if (kind_ != Kind::Array && kind_ != Kind::Object) { return false; }
        return box_ == other.box_;
    }

    // ─── сборка значения из коробки ───
    //
    // Открыты: строку, массив и объект собирает только код, у которого уже
    // есть настоящая коробка нужного типа — а получить такую можно только у
    // detail::makeStringBox/makeArrayBox/makeObjectBox. Тип аргумента и есть
    // защита от значения, указывающего в произвольное место.

    [[nodiscard]] static Value string(detail::StringBox *box) noexcept {
        Value v;
        v.kind_ = Kind::String;
        v.box_ = reinterpret_cast<detail::Box *>(box);
        return v;
    }

    [[nodiscard]] static Value array(detail::ArrayBox *box) noexcept {
        Value v;
        v.kind_ = Kind::Array;
        v.box_ = reinterpret_cast<detail::Box *>(box);
        return v;
    }

    [[nodiscard]] static Value object(detail::ObjectBox *box) noexcept {
        Value v;
        v.kind_ = Kind::Object;
        v.box_ = reinterpret_cast<detail::Box *>(box);
        return v;
    }

   private:
    Value() noexcept : kind_(Kind::Null), number_(0.0) {}

    Kind kind_;  // смещение 0
    union {  // смещение 8: выравнивание double требует зазора после kind_
        bool boolean_;
        double number_;
        detail::Box *box_;  // коробка со счётчиком ссылок
    };
};

static_assert(sizeof(Value) == 16, "Value должен оставаться в 16 байтах");
static_assert(std::is_trivially_copyable_v<Value>,
              "диапазоны значений копируются в пулах целиком");

}  // namespace CS
