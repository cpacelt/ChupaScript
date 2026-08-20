import XCTest

@testable import ChupaScript

/// Хост-функции: замыкание регистрируется типизированным, `ChupaValue`
/// прикладному коду не виден нигде — ни на входе, ни на выходе.
final class HostFunctionTests: XCTestCase {

    func testTypedClosureReceivesTypedArguments() throws {
        let context = Context()
        try context.register("addUp") { (a: Double, b: Double) -> Double in a + b }
        let expression: ChupaScript.Expression<Double> = try context.compile(expression: "addUp(2, 3)")
        XCTAssertEqual(try expression.eval(), 5)
    }

    func testStringArgumentAndResult() throws {
        let context = Context()
        try context.register("shout") { (text: String) -> String in text.uppercased() }
        try context.set("name", "вася")
        let expression: ChupaScript.Expression<String> = try context.compile(expression: "shout(name)")
        XCTAssertEqual(try expression.eval(), "ВАСЯ")
    }

    /// Виды аргументов проверяет трамплин: движок их не сверяет (спека §10.2).
    func testWrongArgumentTypeBecomesAnError() throws {
        let context = Context()
        try context.register("shout") { (text: String) -> String in text.uppercased() }
        let expression: ChupaScript.Expression<String> = try context.compile(expression: "shout(42)")
        XCTAssertThrowsError(try expression.eval())
    }

    /// Текст брошенной ошибки доезжает до хоста целиком, потому что chupa_fail
    /// копирует байты.
    func testThrownErrorKeepsItsMessage() throws {
        struct Boom: Swift.Error {}
        let context = Context()
        try context.register("boom") { (_: Double) -> Double in throw Boom() }
        let expression: ChupaScript.Expression<Double> = try context.compile(expression: "boom(1)")
        XCTAssertThrowsError(try expression.eval()) { error in
            XCTAssertTrue("\(error)".contains("Boom"))
        }
    }

    /// Грязная функция в выражении отвергается компиляцией, а в скрипте нет.
    func testImpureFunctionIsRefusedInAnExpression() throws {
        let context = Context()
        try context.register("track", flags: [.returnsValue]) { (_: Double) -> Double in 0 }
        XCTAssertThrowsError(try context.compile(expression: "track(1)") as ChupaScript.Expression<Double>)
    }

    /// Замыкание не переживает контекст и не течёт: release снимает удержание.
    func testClosureIsReleasedWithTheContext() throws {
        final class Witness { static var alive = 0; init() { Witness.alive += 1 }
                              deinit { Witness.alive -= 1 } }
        XCTAssertEqual(Witness.alive, 0)
        do {
            let witness = Witness()
            let context = Context()
            try context.register("keep") { (_: Double) -> Double in
                _ = witness
                return 0
            }
            XCTAssertEqual(Witness.alive, 1)
            _ = context
        }
        XCTAssertEqual(Witness.alive, 0)
    }
}
