import XCTest
@testable import ChupaScript

final class CachedExpressionTests: XCTestCase {

    func testRepeatedReadsDoNotReenterTheEngine() throws {
        let ctx = Context()
        try ctx.set("name", "Вася")
        let cached = CachedExpression(try ctx.compile(expression: "name") as ChupaScript.Expression<String>)

        XCTAssertEqual(try cached.value(), "Вася")
        XCTAssertEqual(try cached.value(), "Вася")
        XCTAssertEqual(cached.missCount, 1, "второй кадр обязан обойтись без входа в C")
    }

    func testAWriteIsSeenOnTheNextRead() throws {
        let ctx = Context()
        try ctx.set("name", "Вася")
        let cached = CachedExpression(try ctx.compile(expression: "name") as ChupaScript.Expression<String>)
        XCTAssertEqual(try cached.value(), "Вася")

        try ctx.set("name", "Петя")

        XCTAssertEqual(try cached.value(), "Петя")
        XCTAssertEqual(cached.missCount, 2)
    }

    func testTouchingANeighbourDoesNotMiss() throws {
        // То, ради чего схема с коробками стоит своих денег (спека §3.2).
        let ctx = Context()
        try ctx.set("users", text: "[{'name': 'Вася'}, {'name': 'Петя'}]")
        let cached = CachedExpression(try ctx.compile(expression: "users[0].name") as ChupaScript.Expression<String>)
        XCTAssertEqual(try cached.value(), "Вася")

        try ctx.run(try ctx.compile(script: "users[1].name = 'Аня';"))

        XCTAssertEqual(try cached.value(), "Вася")
        XCTAssertEqual(cached.missCount, 1)
    }

    func testADependencyOutlivingItsVariableIsStillSafeToRead() throws {
        // Читатель держит коробки из набора ретейном, поэтому переписанная
        // переменная не уводит из-под него память (спека §2.7). Проверяется
        // под ASan сборкой пакета: `swift test -Xswiftc -sanitize=address`.
        let ctx = Context()
        try ctx.set("users", text: "[{'name': 'Вася'}]")
        let cached = CachedExpression(try ctx.compile(expression: "users[0].name") as ChupaScript.Expression<String>)
        XCTAssertEqual(try cached.value(), "Вася")

        try ctx.set("users", text: "[{'name': 'Петя'}]")

        XCTAssertEqual(try cached.value(), "Петя")
    }

    func testAnUncacheableExpressionRecomputesEveryTime() throws {
        // Выражение с некэшируемым вызовом: n == CHUPA_DEPS_OVERFLOW, читатель
        // ставит себе флаг один раз и больше в набор не смотрит.
        let ctx = Context()
        try ctx.register("now", flags: [.returnsValue, .effectFree]) { 7.0 }
        let cached = CachedExpression(
            try ctx.compile(expression: "format('${}', now())") as ChupaScript.Expression<String>)

        _ = try cached.value()
        _ = try cached.value()

        XCTAssertEqual(cached.missCount, 2)
    }

    func testAConstantNeverMissesTwice() throws {
        let ctx = Context()
        let cached = CachedExpression(try ctx.compile(expression: "42") as ChupaScript.Expression<Double>)

        XCTAssertEqual(try cached.value(), 42)
        XCTAssertEqual(try cached.value(), 42)
        XCTAssertEqual(cached.missCount, 1)
    }
}
