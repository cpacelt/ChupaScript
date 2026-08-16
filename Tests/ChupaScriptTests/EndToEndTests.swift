import XCTest
@testable import ChupaScript

/// Сквозная проверка Swift-обвязки: контекст, глобальные, компиляция,
/// вычисление, ошибка.
///
/// Тесты намеренно поверхностные — глубина покрытия языка живёт в наборе на
/// C++ (`core/tests/`). Здесь проверяется только то, чего тот набор проверить
/// не может: что обвязка собирается как продукт, что вызовы через C API
/// доходят до движка и что значения возвращаются обратно неискажёнными.
final class EndToEndTests: XCTestCase {

    // MARK: - Базовые типы

    func testNumberRoundTripsThroughTheEngine() throws {
        let context = CSContext()
        try context.set("price", 19.99)
        try context.set("count", 3.0)

        let total: CSExpression<Double> = try context.compile(expression: "price * count")
        XCTAssertEqual(try XCTUnwrap(total.eval()), 59.97, accuracy: 1e-9)
    }

    func testBooleanRoundTripsThroughTheEngine() throws {
        let context = CSContext()
        try context.set("enabled", true)

        let yes = try context.compile(expression: "enabled", as: Bool.self)
        let no = try context.compile(expression: "!enabled", as: Bool.self)
        XCTAssertEqual(try yes.eval(), true)
        XCTAssertEqual(try no.eval(), false)
    }

    func testStringRoundTripsThroughTheEngine() throws {
        let context = CSContext()
        try context.set("name", "Мир")

        // Оператора конкатенации в языке нет — строки собирает format
        // (docs/semantics.md §8).
        let greeting = try context.compile(expression: "format('Привет, ${}!', name)",
                                           as: String.self)
        XCTAssertEqual(try greeting.eval(), "Привет, Мир!")
    }

    /// Число, приведённое к строке, проходит через `CS::formatNumber`, а тот —
    /// через вендоренную double-conversion. Тест сторожит именно эту границу:
    /// на платформах Apple плавающий `<charconv>` недоступен, и подмена его
    /// собой не должна менять контракт `docs/semantics.md` §4.3.
    func testNumberToStringFollowsTheSpec() throws {
        let context = CSContext()

        let cases: [(Double, String)] = [
            (1, "1"),
            (1.5, "1.5"),
            (1_000_000, "1000000"),
            (0.1 + 0.2, "0.30000000000000004"),
            (1e21, "1e+21"),
            (1e-8, "1e-8"),
        ]
        for (value, expected) in cases {
            try context.set("x", value)
            let text = try context.compile(expression: "str(x)", as: String.self)
            XCTAssertEqual(try text.eval(), expected, "\(value)")
        }
    }

    // MARK: - Обёртки над базовыми типами

    /// Одно слово в объявлении — и всё: тело разбора приезжает из умолчания на
    /// `CSValue`, писать его не надо.
    enum Align: String, CSValue {
        case left, right
    }

    struct Ratio: RawRepresentable, CSValue, Equatable {
        var rawValue: Double
    }

    func testRawRepresentableEnumNeedsOnlyTheConformance() throws {
        let context = CSContext()
        try context.set("align", "right")

        let align: CSExpression<Align> = try context.compile(expression: "align")
        XCTAssertEqual(try align.eval(), .right)
    }

    func testRawRepresentableOverDoubleWorksToo() throws {
        let context = CSContext()
        try context.set("ratio", 0.75)

        let ratio = try context.compile(expression: "ratio", as: Ratio.self)
        XCTAssertEqual(try ratio.eval(), Ratio(rawValue: 0.75))
    }

    /// Сырьё корректно, но случая с таким значением в перечислении нет. Это
    /// ошибка обвязки, а не движка: текст выражения безупречен, поэтому
    /// отдельный код и `offset == nil`.
    func testValueOutsideTheEnumIsUnrepresentable() throws {
        let context = CSContext()
        try context.set("align", "centre")

        let align: CSExpression<Align> = try context.compile(expression: "align")
        XCTAssertThrowsError(try align.eval()) { error in
            guard let error = error as? CSError else {
                return XCTFail("ожидалась CSError, получена \(error)")
            }
            XCTAssertEqual(error.code, .unrepresentable)
            XCTAssertNil(error.offset)
            XCTAssertTrue(error.message.contains("centre"), error.message)
        }
    }

    // MARK: - Три исхода вычисления

    func testNullIsAValueNotAnError() throws {
        let context = CSContext()
        try context.set("state", text: "{'missing': null}")

        // Чтение отсутствующего ключа мягкое (docs/semantics.md §6.3).
        let absent = try context.compile(expression: "state.nothingHere", as: Double.self)
        XCTAssertNil(try absent.eval())
    }

    func testWrongTypeThrowsInsteadOfReturningNil() throws {
        let context = CSContext()
        try context.set("name", "Мир")

        let asNumber = try context.compile(expression: "name", as: Double.self)
        XCTAssertThrowsError(try asNumber.eval()) { error in
            XCTAssertEqual((error as? CSError)?.code, .type)
        }
    }

    func testDefaultSwallowsBothNullAndError() throws {
        let context = CSContext()
        try context.set("name", "Мир")
        try context.set("state", text: "{'a': 1}")

        let wrongType = try context.compile(expression: "name", as: Double.self)
        XCTAssertEqual(wrongType.eval(default: -1), -1)

        // Причину проглоченной ошибки видно — но только сразу: C API чистит её
        // перед каждым следующим вычислением, включая успешное.
        XCTAssertEqual(context.error?.code, .type)

        let null = try context.compile(expression: "state.missing", as: Double.self)
        XCTAssertEqual(null.eval(default: -1), -1)
        XCTAssertNil(context.error)
    }

    // MARK: - Ошибки записи

    /// Раньше скалярные сеттеры имя не проверяли: запись удавалась молча, а
    /// обратиться к глобальной переменной было нельзя, и хост узнавал об этом
    /// синтаксической ошибкой в другом месте.
    func testScalarSettersRejectUnreferenceableNames() {
        let context = CSContext()

        for name in ["my name", "", "1abc", "true"] {
            XCTAssertThrowsError(try context.set(name, 1.0), name) { error in
                XCTAssertEqual((error as? CSError)?.code, .name, name)
            }
            XCTAssertThrowsError(try context.set(name, true), name)
            XCTAssertThrowsError(try context.set(name, "v"), name)
        }
    }

    /// Текст значения приходит с бэкенда, поэтому его отказ — не ошибка кода, и
    /// код у него другой: `.data`, а не `.name`.
    func testUnparseableValueTextIsRejectedAsData() {
        let context = CSContext()

        XCTAssertThrowsError(try context.set("x", text: "1 + 2")) { error in
            XCTAssertEqual((error as? CSError)?.code, .data)
        }
    }

    // MARK: - Ошибки компиляции

    func testCompileErrorCarriesCodeAndOffset() {
        let context = CSContext()

        XCTAssertThrowsError(try context.compile(expression: "1 +", as: Double.self)) { error in
            guard let error = error as? CSError else {
                return XCTFail("ожидалась CSError, получена \(error)")
            }
            XCTAssertEqual(error.code, .syntax)
            XCTAssertNotNil(error.offset)
            XCTAssertFalse(error.message.isEmpty)
        }
    }

    func testUnknownGlobalIsRejectedAtCompileTime() {
        let context = CSContext()

        XCTAssertThrowsError(
            try context.compile(expression: "nosuchthing + 1", as: Double.self)
        ) { error in
            XCTAssertEqual((error as? CSError)?.code, .name)
        }
    }

    // MARK: - Время жизни и скрипты

    /// Выражение владеет своим хэндлом и освобождает его само. Проверяется
    /// безопасный порядок: сначала уходит выражение, потом контекст.
    func testExpressionOutlivesItsOwnScope() throws {
        let context = CSContext()
        try context.set("x", 2.0)

        var expression: CSExpression<Double>? = try context.compile(expression: "x + x")
        XCTAssertEqual(try expression?.eval(), 4.0)
        expression = nil
    }

    /// Целью присваивания может быть только путь внутрь агрегата, но не само
    /// имя (`docs/semantics.md` §7.2) — отсюда объект, а не голое число.
    func testScriptAssignsThroughAPath() throws {
        let context = CSContext()
        try context.set("state", text: "{'count': 0}")

        let script = try context.compile(script: "state.count = state.count + 5;")
        try context.run(script)

        let read = try context.compile(expression: "state.count", as: Double.self)
        XCTAssertEqual(try read.eval(), 5.0)
    }
}
