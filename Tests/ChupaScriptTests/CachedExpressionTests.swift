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

    // MARK: - Разбор бросает между захватом и присвоением (ревью, Critical)
    //
    // capture() удерживает новых владельцев и переписывает epochs/snapshot;
    // T.chupaValue разбирает уже вычисленное значение. Если разбор бросает —
    // штатно, при несовпадении вида, — а capture успел отработать первым,
    // читатель раздваивается: снимок уже новый, а cached/hasValue — от
    // прошлого чтения. Фикс переставляет порядок: разбор идёт первым, и
    // capture зовётся только когда он удался.

    func testAThrowingParseAfterASuccessfulReadDoesNotSilentlyReturnAStaleValue() throws {
        let ctx = Context()
        try ctx.set("v", 42.0)
        let cached = CachedExpression(try ctx.compile(expression: "v") as ChupaScript.Expression<Double>)
        XCTAssertEqual(try cached.value(), 42)
        XCTAssertEqual(cached.missCount, 1)

        // Тот же адрес эпохи, что и раньше, — переменная не пересоздаётся, —
        // но значение под ним больше не число. Второе чтение обязано войти в
        // движок (эпоха сдвинулась) и бросить: вид не совпал.
        try ctx.set("v", "not a number")
        XCTAssertThrowsError(try cached.value())
        XCTAssertEqual(cached.missCount, 2)

        // Третье чтение — без единой новой правки переменной. С багом
        // capture() уже успел бы захватить снимок второго чтения ДО броска,
        // и сумма эпох совпала бы: читатель отдал бы кэшированные 42 молча,
        // не входя в движок и не бросая. Правильный исход — либо снова
        // войти в движок и бросить, либо хотя бы не соврать значением.
        XCTAssertThrowsError(try cached.value())
        XCTAssertEqual(cached.missCount, 3,
                        "непойманный бросок не должен превращать читателя в вечное попадание по устаревшему значению")
    }

    func testAThrowingParseOnTheFirstCaptureLeavesTheReaderUsable() throws {
        // Первый захват — hasValue ещё false. С багом releaseOwners() внутри
        // повторного capture() смотрит на hasValue, видит false и не
        // отпускает владельцев, удержанных провалившейся первой попыткой, —
        // они просто теряются, затёртые новым захватом. Фикс убирает захват
        // из-под броска целиком: провалившийся разбор не успевает ничего
        // удержать, отпускать после него нечего.
        let ctx = Context()
        try ctx.set("users", text: "[{'name': 'Вася'}]")
        let cached = CachedExpression(try ctx.compile(expression: "users[0].name") as ChupaScript.Expression<Double>)

        XCTAssertThrowsError(try cached.value())
        XCTAssertEqual(cached.missCount, 1)

        try ctx.run(try ctx.compile(script: "users[0].name = 42;"))

        // Читатель остаётся рабочим: следующий вход честно пересчитывает и
        // отдаёт верное значение, а не застревает в испорченном состоянии.
        XCTAssertEqual(try cached.value(), 42)
        XCTAssertEqual(cached.missCount, 2)
    }
}
