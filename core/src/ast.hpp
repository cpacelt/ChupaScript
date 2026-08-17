#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

#include "token.hpp"
#include "value.hpp"

namespace CS {

/// Вид узла дерева разбора. Соответствует docs/grammar.md §5.
///
/// Порядок значим: узлы с детьми идут подряд и раньше листьев, поэтому «есть ли
/// у этого вида дети» — одно сравнение. На нём стоит защита childCount(): у
/// листа поле числа детей занято полезной нагрузкой (см. Node), и читать его
/// как число детей нельзя.
enum class NodeKind : std::uint8_t {
    Invalid,  ///< узел с индексом kNoNode; в готовом дереве не встречается

    // ─── с детьми ───
    Script,        ///< дети: стейтменты
    Assign,         ///< дети: цель, значение; op — один из = += -= *= /=
    CallStatement,  ///< дети: вызов

    Conditional,  ///< дети: условие, ветвь-да, ветвь-нет
    Binary,       ///< дети: левый, правый; op — оператор
    Unary,        ///< дети: операнд; op — ! либо -
    Index,        ///< дети: база, индекс
    Member,       ///< дети: база; текст — имя поля
    Call,         ///< дети: аргументы; текст — имя функции
    Array,        ///< дети: элементы
    Object,       ///< дети: чередование ключ, значение, ключ, значение

    // ─── листья ───
    Identifier,  ///< текст — имя
    Number,      ///< number — значение
    String,      ///< текст — содержимое без кавычек; hasEscape
    Boolean,     ///< boolean — значение
    Null
};

/// Последний вид, у которого бывают дети. Граница между двумя половинами
/// перечисления выше.
inline constexpr NodeKind kLastKindWithChildren = NodeKind::Object;


using NodeId = std::uint32_t;

/// Отсутствие узла. Индекс 0 занят узлом-пустышкой вида Invalid.
inline constexpr NodeId kNoNode = 0;

/// Дерево разбора: хранение, строитель, аксессоры.
///
/// Ничем не владеет: текст имён и литералов — смещения в исходнике, который
/// передаётся аксессорам параметром.
class Ast {
   public:
    Ast();

    /// Начинает новое дерево над исходником такой длины.
    ///
    /// Выбрасывает всё, что было построено раньше: Ast пригоден для повторного
    /// разбора. Самого исходника дерево не держит — узлы хранят смещения, а
    /// байты приходят параметром в text(). Поэтому Ast безразличен к тому,
    /// куда переехал буфер (docs/backlog.md B39).
    void reset(std::uint32_t sourceLength);

    /// Объявляет узел корнем дерева.
    void setRoot(NodeId node) noexcept;

    /// Помечает дерево прошедшим статические проверки (core/src/check.hpp).
    ///
    /// Ставит её только check при нуле находок; вычислитель требует её
    /// утверждением. Так «забыли проверить» падает на первом же тесте, а в
    /// релизе не стоит ничего.
    void markChecked() noexcept { checked_ = true; }
    [[nodiscard]] bool isChecked() const noexcept { return checked_; }

    // ─── строитель: единственный способ создать узел ───

    NodeId number(const Token &token);
    NodeId string(const Token &token);
    NodeId boolean(const Token &token);
    NodeId null(const Token &token);
    NodeId identifier(const Token &token);
    NodeId member(NodeId base, const Token &name);
    NodeId index(NodeId base, NodeId subscript, std::uint32_t offset);
    NodeId unary(TokenKind op, NodeId operand, std::uint32_t offset);
    NodeId binary(TokenKind op, NodeId lhs, NodeId rhs, std::uint32_t offset);
    NodeId conditional(NodeId condition, NodeId whenTrue, NodeId whenFalse,
                       std::uint32_t offset);
    NodeId call(const Token &name, const NodeId *args, std::uint32_t count);
    NodeId array(const NodeId *items, std::uint32_t count, std::uint32_t offset);
    NodeId object(const NodeId *pairs, std::uint32_t count, std::uint32_t offset);
    NodeId assign(TokenKind op, NodeId target, NodeId value, std::uint32_t offset);
    NodeId callStatement(NodeId callNode, std::uint32_t offset);
    NodeId script(const NodeId *statements, std::uint32_t count);

    // ─── аксессоры: единственный способ прочитать узел ───

    /// Корень дерева; kNoNode, если разбор не удался.
    [[nodiscard]] NodeId root() const noexcept;

    [[nodiscard]] NodeKind kind(NodeId node) const noexcept;
    [[nodiscard]] TokenKind op(NodeId node) const noexcept;
    [[nodiscard]] std::uint32_t offset(NodeId node) const noexcept;

    [[nodiscard]] std::uint32_t childCount(NodeId node) const noexcept;
    /// kNoNode, если index за границей диапазона детей.
    [[nodiscard]] NodeId child(NodeId node, std::uint32_t index) const noexcept;

    [[nodiscard]] double numberValue(NodeId node) const noexcept;
    [[nodiscard]] bool boolValue(NodeId node) const noexcept;
    /// Имя либо содержимое строкового литерала без кавычек, сырыми байтами.
    ///
    /// source обязан быть тем же текстом, над которым дерево построено.
    /// В отладочной сборке несовпадение ловится утверждением по длине.
    [[nodiscard]] std::string_view text(NodeId node,
                                        std::string_view source) const noexcept;

    /// Длина исходника, над которым построено дерево. Для утверждений и тестов.
    [[nodiscard]] std::uint32_t sourceLength() const noexcept;

    [[nodiscard]] bool hasEscape(NodeId node) const noexcept;

    /// Номер ячейки значения глобальной переменной для узла Identifier.
    ///
    /// Разрешает его check (core/src/check.hpp) — единственное место, где имя
    /// вообще ищется, — и вычислению остаётся индексация вместо поиска.
    ///
    /// Предусловие: имя разрешено. Неразрешённое до вычисления не доходит:
    /// компиляция отвергает его ошибкой Name.
    [[nodiscard]] GlobalSlot globalValuesSlot(NodeId node) const noexcept;

    /// Записывает разрешённый номер. Зовёт только check.
    void setGlobalValuesSlot(NodeId node, GlobalSlot slot) noexcept;

    /// Разрешено ли имя. Нулевая ячейка от неразрешённой иначе не отличается:
    /// прежнего значения-сторожа в узле больше нет, эти байты делят с номером
    /// ячейки другие виды узлов (см. Node).
    [[nodiscard]] bool hasGlobalValuesSlot(NodeId node) const noexcept;

    /// Число узлов, включая пустышку с индексом kNoNode. Для тестов и замеров.
    [[nodiscard]] std::uint32_t nodeCount() const noexcept;

   private:
    /// Узел дерева — двадцать четыре байта.
    ///
    /// Раскладка скрыта языком, а не соглашением: Node — приватный вложенный
    /// тип Ast, и вне ast.cpp к полям обратиться нельзя. Единственный доступ
    /// снаружи — через аксессоры Ast. Это шов, ради которого решения B6–B10
    /// остаются отложенными, и он же позволил ужать узел с пятидесяти шести
    /// байт до нынешних, не тронув ни одного вызывающего.
    ///
    /// Плотность держится на двух наблюдениях:
    ///
    /// - Смещение текста выводится и потому не хранится. У Identifier, Member и
    ///   Call имя начинается ровно там, куда указывает offset; у String
    ///   содержимое начинается байтом позже — кавычка однобайтовая
    ///   (core/src/token.hpp, stringContentOffset). Хранить эту величину
    ///   отдельно значило бы держать копию offset.
    ///
    /// - Толстая нагрузка принадлежит узлам без детей. Число бывает только у
    ///   Number, номер ячейки — только у Identifier, и ни тот ни другой вид
    ///   детей не имеет. Поэтому оба делят те же восемь байт, где у прочих
    ///   лежат начало и число детей.
    ///
    /// Цена — одна: у листа поле числа детей занято чужими байтами, и
    /// childCount() обязана вернуть ноль по виду, не заглядывая в него. Ради
    /// того, чтобы это стоило одного сравнения, виды с детьми стоят в
    /// NodeKind подряд.
    struct Node {
        NodeKind kind = NodeKind::Invalid;  ///< смещение 0
        TokenKind op = TokenKind::End;      ///< 1 — Unary, Binary, Assign
        /// 2 — набор битов kFlag* (ast.cpp): экранирование у String, значение
        /// у Boolean, признак разрешённого имени у Identifier. Биты вместо
        /// байтов: каждый принадлежит одному виду узла и с прочими не
        /// встречается.
        std::uint8_t flags = 0;
        // 3 — байт выравнивания.

        std::uint32_t offset = 0;  ///< 4 — место узла в исходнике; у всех

        /// 8 — нагрузка, своя у каждой половины перечисления видов.
        ///
        /// Инициализатор один на всё объединение — так устроен язык, — и это
        /// именно тот член, который нужен: узлы с детьми составляют большую
        /// часть дерева, а нули означают «детей нет», что верно и для
        /// узла-пустышки с индексом kNoNode.
        union Payload {
            struct {
                std::uint32_t start;
                std::uint32_t count;
            } children;                   ///< виды до kLastKindWithChildren
            double number;                ///< Number
            GlobalSlot globalValuesSlot;  ///< Identifier, после прохода
        } payload = {{0, 0}};

        /// 16 — длина имени либо сырого содержимого литерала в исходнике.
        std::uint32_t textLength = 0;
        // 20 — четыре байта выравнивания под double в объединении.
    };

    static_assert(sizeof(Node) == 24, "узел обязан оставаться в 24 байтах");

    NodeId add(const Node &node);
    std::uint32_t pushChildren(const NodeId *ids, std::uint32_t count);

    std::uint32_t sourceLength_ = 0;
    NodeId root_ = kNoNode;
    bool checked_ = false;
    std::vector<Node> nodes_;      // TODO(B7): переехать в арену хранилища
    std::vector<NodeId> children_; // TODO(B10): боковой пул детей
};

}  // namespace CS
