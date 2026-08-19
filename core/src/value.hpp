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

/// Значение ChupaScript. Шесть типов из docs/semantics.md §2.1.
///
/// Агрегат и долгоживущая строка адресуются указателем на коробку со
/// счётчиком ссылок: такое значение самодостаточно и читается без хранилища
/// вовсе. Промежуточная строка адресуется смещением в арену операции, и вот
/// она без своего хранилища бессмысленна — потому её фабрика и закрыта.
///
/// Раскладка и обоснование:
/// docs/superpowers/specs/2026-08-11-chupascript-values-design.md §4.
class Value {
   public:
    /// Вид значения. Закрытый список из docs/semantics.md §2.1.
    enum class Kind : std::uint8_t { Null, Boolean, Number, String, Object, Array };

    /// Где лежит нагрузка значения. Ось одна, и члены обязаны отвечать на этот
    /// вопрос, а не на вопрос о владении.
    ///
    /// Раньше здесь стояла шкала времени жизни — постоянное против
    /// временного, — и на её порядке держался барьер записи. Барьера больше
    /// нет: коробка не может оказаться короткоживущей, чем контейнер, за это
    /// отвечает счётчик ссылок.
    ///
    /// Boxed — в объединении указатель на коробку; значение самодостаточно и
    /// читается без всякого хранилища, потому и уезжает к хосту ссылкой.
    /// Scratch — в объединении смещение в байтовую арену операции; так
    /// адресуются только строки, и только промежуточные.
    ///
    /// Третьим членом сюда встанет Inline — байты короткой строки в самом
    /// значении, без коробки и без счётчика. Ось от этого не меняется, и в
    /// этом весь довод за такие имена: «упаковано» и «в арене» — про место,
    /// а не про то, как владеем.
    ///
    /// Разбор: docs/superpowers/specs/2026-08-19-chupascript-memory-model-design.md Р2.
    enum class Region : std::uint8_t { Boxed, Scratch };

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

    /// Осмыслен только у String, Object и Array — см. addressesStore.
    [[nodiscard]] Region region() const noexcept { return region_; }

    /// Коробка, на которую значение ссылается.
    /// Предусловие: addressesStore() и region() == Region::Boxed.
    [[nodiscard]] detail::Box *box() const noexcept {
        assert(addressesStore() && region_ == Region::Boxed);
        return box_;
    }

    /// Адресует ли значение пулы хранилища — то есть осмыслен ли у него
    /// регион. У скаляров нет ни того, ни другого: копия числа ни с каким
    /// хранилищем не связана.
    [[nodiscard]] bool addressesStore() const noexcept {
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

    /// Один ли это агрегат — сравнивает вид и индекс заголовка.
    ///
    /// У скаляров идентичности нет (docs/semantics.md §5.4), для них всегда
    /// false, в том числе при сравнении значения с самим собой.
    [[nodiscard]] bool sameAggregate(Value other) const noexcept {
        if (kind_ != other.kind_) { return false; }
        if (kind_ != Kind::Array && kind_ != Kind::Object) { return false; }
        // Сравнение региона отсюда ушло: у агрегата он всегда Boxed, а
        // личность агрегата — адрес его коробки, а не номер в чьих-то пулах.
        return box_ == other.box_;
    }

    // ─── сборка значения из коробки ───
    //
    // Открыты, в отличие от закрытой строковой фабрики ниже: там довод в том,
    // что смещение полно как тип и любой код собрал бы значение с
    // произвольным числом. С указателем этот довод не работает наоборот —
    // указатель числом не подделаешь, а взять настоящий ArrayBox * можно
    // только у detail::makeArrayBox. Тип аргумента и есть защита.

    [[nodiscard]] static Value string(detail::StringBox *box,
                                      std::uint32_t length) noexcept {
        Value v;
        v.kind_ = Kind::String;
        v.length_ = length;
        v.box_ = reinterpret_cast<detail::Box *>(box);
        v.region_ = Region::Boxed;
        return v;
    }

    [[nodiscard]] static Value array(detail::ArrayBox *box) noexcept {
        Value v;
        v.kind_ = Kind::Array;
        v.box_ = reinterpret_cast<detail::Box *>(box);
        v.region_ = Region::Boxed;
        return v;
    }

    [[nodiscard]] static Value object(detail::ObjectBox *box) noexcept {
        Value v;
        v.kind_ = Kind::Object;
        v.box_ = reinterpret_cast<detail::Box *>(box);
        v.region_ = Region::Boxed;
        return v;
    }

   private:
    friend class Store;

    /// Промежуточная строка: смещение в арену операции и длина.
    ///
    /// Закрыта, потому что смещение полно как тип: без ограничения доступа
    /// любой код собрал бы строку, указывающую в произвольное место арены.
    ///
    /// Региона в параметрах нет: смещение осмысленно ровно в одном регионе, и
    /// раньше он передавался сюда единственным значением. Индексных фабрик для
    /// массива и объекта здесь тоже больше нет — агрегат теперь всегда коробка.
    [[nodiscard]] static Value scratchString(std::uint32_t offset,
                                             std::uint32_t length) noexcept {
        Value v;
        v.kind_ = Kind::String;
        v.length_ = length;
        v.index_ = offset;
        v.region_ = Region::Scratch;
        return v;
    }

    [[nodiscard]] std::uint32_t index() const noexcept {
        assert(kind_ == Kind::String);
        return index_;
    }

    [[nodiscard]] std::uint32_t length() const noexcept {
        assert(kind_ == Kind::String);
        return length_;
    }

    Value() noexcept : kind_(Kind::Null), length_(0), number_(0.0) {}

    Kind kind_;  // смещение 0
    // Смещение 1: байт был набивкой перед length_, поэтому регион достался
    // даром. У скаляров региона нет — они ничего не адресуют; поле у них не
    // читается, проверки региона касаются только String, Object и Array.
    Region region_ = Region::Boxed;
    std::uint32_t length_;  // смещение 4 — длина строки в байтах
    // TODO(B2): восемь байт вместо шестнадцати достижимы только через
    // NaN-boxing: double в объединении задаёт и размер, и выравнивание.
    union {  // смещение 8
        bool boolean_;
        double number_;
        std::uint32_t index_;  // Scratch: смещение в арену операции
        detail::Box *box_;     // Boxed: коробка со счётчиком ссылок
    };
};

static_assert(sizeof(Value) == 16, "Value должен оставаться в 16 байтах");
static_assert(std::is_trivially_copyable_v<Value>,
              "диапазоны значений копируются в пулах целиком");

}  // namespace CS
