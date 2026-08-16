import XCTest
@testable import ChupaScript

/// Обвязка не должна ломаться от того, что чужой модуль добавил конформанс
/// стандартному типу.
///
/// Не гипотетическая опасность: `CourierSwift` из `odnoklassniki-ios`
/// объявляет `extension String: @retroactive RawRepresentable` (ему нужно, чтобы
/// голая строка годилась в ключи batch-запроса наравне с перечислениями), и
/// конформанс достаётся всем, кто линкует модуль. Отменить его нельзя.
///
/// Расширение ниже воспроизводит тот конформанс дословно. Пока `eval()` был
/// объявлен дважды — для `T: CSValue` и для `T: RawRepresentable` — под ним
/// `Expression<String>.eval()` переставал компилироваться: сигнатуры у обеих
/// перегрузок одинаковые (`() throws -> T?`), различали их только ограничения, а
/// при выполнении обоих правила выбора у языка нет. На месте вызова развести их
/// было нечем — ни аннотацией типа, ни `as String?`.
///
/// Набор живёт в отдельном файле именно потому, что конформанс глобальный: он
/// действует на всю тестовую цель, и держать его рядом с остальными тестами
/// значило бы менять условия у них.
extension String: @retroactive RawRepresentable {
    public typealias RawValue = String
    public var rawValue: String { self }
    public init?(rawValue: String) { self = rawValue }
}

final class RetroactiveConformanceTests: XCTestCase {

    /// Базовый случай: `String` — и `CSValue`, и (по милости чужого модуля)
    /// `RawRepresentable`. Вызов обязан остаться однозначным.
    func testStringStaysCallableUnderRetroactiveConformance() throws {
        let context = CSContext()
        try context.set("name", "Мир")

        let name = try context.compile(expression: "name", as: String.self)

        XCTAssertEqual(try name.eval(), "Мир")
    }

    /// Тип, у которого своя реализация есть, обязан брать её, а не умолчание
    /// для обёрток. Иначе `String` с `RawValue == String` ушёл бы разбирать сам
    /// себя — и это была бы бесконечная рекурсия, а не ошибка компиляции.
    func testStringUsesItsOwnWitnessNotTheWrapperDefault() throws {
        let context = CSContext()
        try context.set("name", "Мир")

        let name = try context.compile(expression: "name", as: String.self)

        // Дойти до ответа уже достаточно: умолчание здесь не вернуло бы
        // управление.
        XCTAssertEqual(try name.eval(default: ""), "Мир")
    }

    /// Обёртки под тем же конформансом продолжают работать через умолчание.
    enum Align: String, CSValue {
        case left, right
    }

    func testWrapperStillResolvesUnderRetroactiveConformance() throws {
        let context = CSContext()
        try context.set("align", "right")

        let align: CSExpression<Align> = try context.compile(expression: "align")

        XCTAssertEqual(try align.eval(), .right)
    }
}
