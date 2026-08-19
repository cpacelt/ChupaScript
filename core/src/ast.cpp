#include "ast.hpp"

#include <cassert>

#include "box.hpp"

namespace CS {
namespace {

/// Биты поля Node::flags. Каждый принадлежит одному виду узла, поэтому делят
/// они один байт без всякой оговорки.
constexpr std::uint8_t kFlagEscape = 1u << 0;   ///< String: есть экранирование
constexpr std::uint8_t kFlagBoolean = 1u << 1;  ///< Boolean: значение узла
constexpr std::uint8_t kFlagLiteral = 1u << 2;  ///< String: литерал уложен
constexpr std::uint8_t kFlagSlot = 1u << 3;     ///< Identifier: имя разрешено

/// Бывают ли у этого вида дети. Одно сравнение — на этом стоит порядок
/// перечисления NodeKind.
constexpr bool hasChildren(NodeKind kind) noexcept {
    return kind <= kLastKindWithChildren;
}

}  // namespace

Ast::Ast() {
    // Индекс kNoNode занят пустышкой, чтобы 0 означал «нет узла» и обращение
    // к нему было безопасным.
    nodes_.push_back(Node{});
}

Ast::~Ast() {
    for (detail::StringBox *literal : literals_) { detail::release(literal); }
}

Ast::Ast(Ast &&) noexcept = default;

Ast &Ast::operator=(Ast &&other) noexcept {
    if (this == &other) { return *this; }
    // Release first: assignment overwrites literals_ wholesale, and the boxes
    // this Ast held would otherwise leak.
    for (detail::StringBox *literal : literals_) { detail::release(literal); }
    nodes_ = std::move(other.nodes_);
    children_ = std::move(other.children_);
    literals_ = std::move(other.literals_);
    sourceLength_ = other.sourceLength_;
    root_ = other.root_;
    checked_ = other.checked_;
    return *this;
}

detail::StringBox *Ast::internLiteral(std::string_view bytes) {
    detail::StringBox *box = detail::makeStringBox(bytes);
    literals_.push_back(box);
    return box;
}

void Ast::reset(std::uint32_t sourceLength) {
    sourceLength_ = sourceLength;
    root_ = kNoNode;
    nodes_.clear();
    children_.clear();
    nodes_.push_back(Node{});  // индекс kNoNode
    // The tree is discarded here, and its literals go with it: without
    // release() here, reusing the same Ast for another parse would grow
    // literals_ without bound, since nothing else ever releases the old
    // tree's references.
    for (detail::StringBox *literal : literals_) { detail::release(literal); }
    literals_.clear();
    checked_ = false;
}

void Ast::setRoot(NodeId node) noexcept { root_ = node; }

NodeId Ast::add(const Node &node) {
    nodes_.push_back(node);
    return static_cast<NodeId>(nodes_.size() - 1);
}

std::uint32_t Ast::pushChildren(const NodeId *ids, std::uint32_t count) {
    const auto start = static_cast<std::uint32_t>(children_.size());
    children_.insert(children_.end(), ids, ids + count);
    return start;
}

// ─── строитель ───────────────────────────────────────────────────────

NodeId Ast::number(const Token &token) {
    Node node;
    node.kind = NodeKind::Number;
    node.offset = token.offset;
    node.payload.number = token.number;
    return add(node);
}

NodeId Ast::string(const Token &token) {
    Node node;
    node.kind = NodeKind::String;
    // Смещение содержимого не хранится: оно на байт правее offset, потому что
    // кавычка однобайтовая (token.hpp, stringContentOffset). Смотри textStart.
    node.offset = token.offset;
    node.textLength = stringContentLength(token);
    if (token.hasEscape) { node.flags |= kFlagEscape; }
    return add(node);
}

NodeId Ast::boolean(const Token &token) {
    Node node;
    node.kind = NodeKind::Boolean;
    node.offset = token.offset;
    if (token.kind == TokenKind::True) { node.flags |= kFlagBoolean; }
    return add(node);
}

NodeId Ast::null(const Token &token) {
    Node node;
    node.kind = NodeKind::Null;
    node.offset = token.offset;
    return add(node);
}

NodeId Ast::identifier(const Token &token) {
    Node node;
    node.kind = NodeKind::Identifier;
    node.offset = token.offset;
    node.textLength = token.length;
    return add(node);
}

NodeId Ast::member(NodeId base, const Token &name) {
    Node node;
    node.kind = NodeKind::Member;
    // Смещение — имя поля: на него указывает сообщение об отсутствующем ключе,
    // с него же начинается текст имени.
    node.offset = name.offset;
    node.textLength = name.length;
    node.payload.children.start = pushChildren(&base, 1);
    node.payload.children.count = 1;
    return add(node);
}

NodeId Ast::index(NodeId base, NodeId subscript, std::uint32_t offset) {
    const NodeId kids[2] = {base, subscript};
    Node node;
    node.kind = NodeKind::Index;
    node.offset = offset;
    node.payload.children.start = pushChildren(kids, 2);
    node.payload.children.count = 2;
    return add(node);
}

NodeId Ast::unary(TokenKind op, NodeId operand, std::uint32_t offset) {
    Node node;
    node.kind = NodeKind::Unary;
    node.op = op;
    node.offset = offset;
    node.payload.children.start = pushChildren(&operand, 1);
    node.payload.children.count = 1;
    return add(node);
}

NodeId Ast::binary(TokenKind op, NodeId lhs, NodeId rhs, std::uint32_t offset) {
    const NodeId kids[2] = {lhs, rhs};
    Node node;
    node.kind = NodeKind::Binary;
    node.op = op;
    node.offset = offset;
    node.payload.children.start = pushChildren(kids, 2);
    node.payload.children.count = 2;
    return add(node);
}

NodeId Ast::conditional(NodeId condition, NodeId whenTrue, NodeId whenFalse,
                        std::uint32_t offset) {
    const NodeId kids[3] = {condition, whenTrue, whenFalse};
    Node node;
    node.kind = NodeKind::Conditional;
    node.offset = offset;
    node.payload.children.start = pushChildren(kids, 3);
    node.payload.children.count = 3;
    return add(node);
}

NodeId Ast::call(const Token &name, const NodeId *args, std::uint32_t count) {
    Node node;
    node.kind = NodeKind::Call;
    node.offset = name.offset;
    node.textLength = name.length;
    node.payload.children.start = pushChildren(args, count);
    node.payload.children.count = count;
    return add(node);
}

NodeId Ast::array(const NodeId *items, std::uint32_t count,
                  std::uint32_t offset) {
    Node node;
    node.kind = NodeKind::Array;
    node.offset = offset;
    node.payload.children.start = pushChildren(items, count);
    node.payload.children.count = count;
    return add(node);
}

NodeId Ast::object(const NodeId *pairs, std::uint32_t count,
                   std::uint32_t offset) {
    // count — длина массива, то есть 2n при n парах: дети чередуются.
    Node node;
    node.kind = NodeKind::Object;
    node.offset = offset;
    node.payload.children.start = pushChildren(pairs, count);
    node.payload.children.count = count;
    return add(node);
}

NodeId Ast::assign(TokenKind op, NodeId target, NodeId value,
                   std::uint32_t offset) {
    const NodeId kids[2] = {target, value};
    Node node;
    node.kind = NodeKind::Assign;
    node.op = op;
    node.offset = offset;
    node.payload.children.start = pushChildren(kids, 2);
    node.payload.children.count = 2;
    return add(node);
}

NodeId Ast::callStatement(NodeId callNode, std::uint32_t offset) {
    Node node;
    node.kind = NodeKind::CallStatement;
    node.offset = offset;
    node.payload.children.start = pushChildren(&callNode, 1);
    node.payload.children.count = 1;
    return add(node);
}

NodeId Ast::script(const NodeId *statements, std::uint32_t count) {
    Node node;
    node.kind = NodeKind::Script;
    node.offset = 0;
    node.payload.children.start = pushChildren(statements, count);
    node.payload.children.count = count;
    return add(node);
}

// ─── аксессоры ───────────────────────────────────────────────────────

NodeId Ast::root() const noexcept { return root_; }

NodeKind Ast::kind(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return nodes_[node].kind;
}

TokenKind Ast::op(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return nodes_[node].op;
}

std::uint32_t Ast::offset(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return nodes_[node].offset;
}

std::uint32_t Ast::childCount(NodeId node) const noexcept {
    assert(node < nodes_.size());
    const Node &n = nodes_[node];
    // Проверка по виду обязательна: у листа те же восемь байт заняты числом,
    // номером ячейки либо координатами литерала, и прочитанное оттуда «число
    // детей» увело бы child() за пределы children_.
    return hasChildren(n.kind) ? n.payload.children.count : 0;
}

NodeId Ast::child(NodeId node, std::uint32_t index) const noexcept {
    assert(node < nodes_.size());
    const Node &n = nodes_[node];
    if (!hasChildren(n.kind) || index >= n.payload.children.count) {
        return kNoNode;
    }
    return children_[n.payload.children.start + index];
}

double Ast::numberValue(NodeId node) const noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::Number && "число бывает только у Number");
    return nodes_[node].payload.number;
}

bool Ast::boolValue(NodeId node) const noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::Boolean &&
           "истинность бывает только у Boolean");
    return (nodes_[node].flags & kFlagBoolean) != 0;
}

std::uint32_t Ast::sourceLength() const noexcept { return sourceLength_; }

namespace {

/// Где в исходнике начинается текст узла.
///
/// Отдельного поля под это нет: у Identifier, Member и Call имя начинается там
/// же, куда указывает offset, а у String содержимое — байтом правее, потому что
/// кавычка однобайтовая (core/src/token.hpp, stringContentOffset). Хранить эту
/// величину значило бы держать копию offset у трёх видов из четырёх.
std::uint32_t textStart(NodeKind kind, std::uint32_t offset) noexcept {
    return kind == NodeKind::String ? offset + 1 : offset;
}

}  // namespace

std::string_view Ast::text(NodeId node, std::string_view source) const noexcept {
    assert(node < nodes_.size());
    // Дешёвая ловушка на «передали не тот исходник» (спека Р3).
    assert(source.size() == sourceLength_);
    const Node &n = nodes_[node];
    if (n.textLength == 0) { return {}; }
    const std::uint32_t start = textStart(n.kind, n.offset);
    assert(start + n.textLength <= source.size());
    // Не substr: он бросает out_of_range при start > size(), а text()
    // объявлена noexcept — в релизной сборке, где assert'ы выше сняты, это
    // был бы std::terminate вместо мусора. Конструктор среза не бросает.
    return std::string_view(source.data() + start, n.textLength);
}

bool Ast::hasEscape(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return (nodes_[node].flags & kFlagEscape) != 0;
}

GlobalSlot Ast::globalValuesSlot(NodeId node) const noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::Identifier &&
           "ячейка бывает только у обращения к имени");
    assert(hasGlobalValuesSlot(node) && "имя обязано быть разрешено проходом");
    return nodes_[node].payload.globalValuesSlot;
}

void Ast::setGlobalValuesSlot(NodeId node, GlobalSlot slot) noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::Identifier &&
           "ячейка бывает только у обращения к имени");
    assert(slot != kNoGlobalSlot && "разрешением kNoGlobalSlot не бывает");
    nodes_[node].payload.globalValuesSlot = slot;
    nodes_[node].flags |= kFlagSlot;
}

bool Ast::hasGlobalValuesSlot(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return (nodes_[node].flags & kFlagSlot) != 0;
}

detail::StringBox *Ast::stringLiteral(NodeId node) const noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::String &&
           "уложенный литерал бывает только у строкового литерала");
    assert(hasStringLiteral(node) && "литерал обязан быть уложен");
    return nodes_[node].payload.literal;
}

void Ast::setStringLiteral(NodeId node, detail::StringBox *literal) noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::String &&
           "уложенный литерал бывает только у строкового литерала");
    nodes_[node].payload.literal = literal;
    nodes_[node].flags |= kFlagLiteral;
}

bool Ast::hasStringLiteral(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return (nodes_[node].flags & kFlagLiteral) != 0;
}

Builtin Ast::builtinId(NodeId node) const noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::Call &&
           "функция бывает только у вызова");
    assert(nodes_[node].builtin != kNoBuiltin &&
           "имя обязано быть разрешено проходом");
    return nodes_[node].builtin;
}

void Ast::setBuiltinId(NodeId node, Builtin id) noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::Call &&
           "функция бывает только у вызова");
    assert(id != kNoBuiltin && "разрешением kNoBuiltin не бывает");
    nodes_[node].builtin = id;
}

bool Ast::hasBuiltinId(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return nodes_[node].builtin != kNoBuiltin;
}

std::uint32_t Ast::nodeCount() const noexcept {
    return static_cast<std::uint32_t>(nodes_.size());
}

}  // namespace CS
