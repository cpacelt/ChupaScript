#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace CS {

/// Ключа нет. Номером ключа быть не может: столько их не бывает.
inline constexpr std::uint32_t kNoKey = 0xffffffffu;

/// Таблица имён полей: байты ключа лежат в ней в единственном экземпляре, а
/// объекты держат четырёхбайтовый номер.
///
/// Ключи — не данные общего вида: их мало, они повторяются тысячами и почти
/// все известны на компиляции. Приходят они из литерала объекта, присваивания
/// в поле и разбора данных от хоста; ключей, придуманных на кадре скролла, не
/// бывает. Поэтому внутри таблицы поштучного учёта нет вовсе — это арена,
/// только дописывание.
///
/// Счётчик ссылок стоит на таблице целиком, а не на имени: её держит каждый
/// коробка-объект, и она переживает контекст ровно тогда, когда её пережил хоть
/// один объект, уехавший к хосту. Иначе у такого объекта повисли бы ключи.
///
/// Обоснование: docs/superpowers/specs/2026-08-19-chupascript-memory-model-design.md Р5.
class KeyTable {
   public:
    /// Новая таблица со счётчиком 1.
    static KeyTable *create();
    static void retain(KeyTable *table) noexcept;
    /// Отпускает ссылку; на нуле разрушает таблицу.
    static void release(KeyTable *table) noexcept;

    KeyTable(const KeyTable &) = delete;
    KeyTable &operator=(const KeyTable &) = delete;

    /// Номер ключа, при надобности заводит новый.
    std::uint32_t intern(std::string_view key);

    /// Номер ключа либо kNoKey. В таблицу не пишет — нужен чтению объекта,
    /// которое не должно засорять её именами, которых в нём нет.
    [[nodiscard]] std::uint32_t find(std::string_view key) const noexcept;

    /// Байты ключа по номеру. Срез действителен, пока таблица жива и в неё не
    /// дописали: рост арены переселяет буфер.
    [[nodiscard]] std::string_view bytes(std::uint32_t id) const noexcept;

    [[nodiscard]] std::uint32_t count() const noexcept;

   private:
    KeyTable() = default;
    ~KeyTable() = default;

    /// Координаты ключа в арене имён.
    struct Rec {
        std::uint32_t offset;
        std::uint32_t length;
    };

    /// Место в sorted_, где номер стоит либо должен встать; found — признак
    /// находки. Тот же двоичный поиск, что был у Store::findKey.
    std::uint32_t place(std::string_view key, bool *found) const noexcept;

    std::uint32_t rc_ = 1;
    std::vector<char> text_;             // байты имён подряд, только дописывание
    std::vector<Rec> byId_;              // номер → координаты
    std::vector<std::uint32_t> sorted_;  // номера, упорядоченные по байтам
};

}  // namespace CS
