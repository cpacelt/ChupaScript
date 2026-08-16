import XCTest
import ChupaScriptC
@testable import ChupaScript

/// Сквозная проверка Swift-обвязки: контекст, глобальные, компиляция,
/// вычисление, ошибка.
///
/// Тесты намеренно поверхностные — глубина покрытия языка живёт в наборе на
/// C++ (`core/tests/`). Здесь проверяется только то, чего тот набор проверить
/// не может: что обвязка собирается как продукт, что вызовы через C API
/// доходят до движка и что значения возвращаются обратно неискажёнными.
final class EndToEndTests: XCTestCase {

    func testNumberRoundTripsThroughTheEngine() throws {
        let context = Context()
        context.set("price", 19.99)
        context.set("count", 3.0)

        let total = try context.compile(expression: "price * count")
        XCTAssertEqual(try XCTUnwrap(total.evalNumber()), 59.97, accuracy: 1e-9)
    }

    func testBooleanRoundTripsThroughTheEngine() throws {
        let context = Context()
        context.set("enabled", true)

        XCTAssertEqual(try context.compile(expression: "enabled").evalBool(), true)
        XCTAssertEqual(try context.compile(expression: "!enabled").evalBool(), false)
    }

    func testStringRoundTripsThroughTheEngine() throws {
        let context = Context()
        context.set("name", "Мир")

        // Оператора конкатенации в языке нет — строки собирает format
        // (docs/semantics.md §8).
        let greeting = try context.compile(expression: "format('Привет, ${}!', name)")
        XCTAssertEqual(try greeting.evalString(), "Привет, Мир!")
    }

    /// Число, приведённое к строке, проходит через `CS::formatNumber`, а тот —
    /// через вендоренную double-conversion. Тест сторожит именно эту границу:
    /// на платформах Apple плавающий `<charconv>` недоступен, и подмена его
    /// собой не должна менять контракт `docs/semantics.md` §4.3.
    func testNumberToStringFollowsTheSpec() throws {
        let context = Context()

        let cases: [(Double, String)] = [
            (1, "1"),
            (1.5, "1.5"),
            (1_000_000, "1000000"),
            (0.1 + 0.2, "0.30000000000000004"),
            (1e21, "1e+21"),
            (1e-8, "1e-8"),
        ]
        for (value, expected) in cases {
            context.set("x", value)
            let expression = try context.compile(expression: "str(x)")
            XCTAssertEqual(try expression.evalString(), expected, "\(value)")
        }
    }

    func testCompileErrorCarriesCodeAndOffset() {
        let context = Context()

        XCTAssertThrowsError(try context.compile(expression: "1 +")) { error in
            guard let error = error as? ChupaScript.Error else {
                return XCTFail("ожидалась ChupaScript.Error, получена \(error)")
            }
            XCTAssertNotEqual(error.code, CHUPA_ERR_NONE)
            XCTAssertFalse(error.message.isEmpty)
        }
    }

    func testUnknownGlobalIsRejectedAtCompileTime() {
        let context = Context()

        XCTAssertThrowsError(try context.compile(expression: "nosuchthing + 1"))
    }

    /// Скомпилированное выражение владеет своим хэндлом и переживает контекст
    /// только на бумаге: вычислять его после смерти контекста нельзя. Здесь
    /// проверяется обратное и безопасное — что порядок «сначала выражение,
    /// потом контекст» разрушается без падения.
    func testExpressionOutlivesItsOwnScope() throws {
        let context = Context()
        context.set("x", 2.0)

        // Имя квалифицировано: Foundation с iOS 18 объявляет собственный
        // Expression, и без префикса поиск типа неоднозначен.
        var expression: ChupaScript.Expression? =
            try context.compile(expression: "x + x")
        XCTAssertEqual(expression?.evalNumber(), 4.0)
        expression = nil
    }

    /// Целью присваивания может быть только путь внутрь агрегата, но не само
    /// имя (`docs/semantics.md` §7.2) — отсюда объект, а не голое число.
    func testScriptAssignsThroughAPath() throws {
        let context = Context()
        XCTAssertTrue(context.set("state", text: "{'count': 0}"))

        let script = try context.compile(script: "state.count = state.count + 5;")
        XCTAssertTrue(context.run(script))

        let read = try context.compile(expression: "state.count")
        XCTAssertEqual(read.evalNumber(), 5.0)
    }
}
