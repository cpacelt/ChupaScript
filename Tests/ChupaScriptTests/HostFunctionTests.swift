import XCTest

import ChupaScriptC
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

    /// Типизированная перегрузка без `.returnsValue` компилируется — `R` с
    /// флагами не связан ничем, — а движок звал бы такую функцию с
    /// `out == nullptr`. Прежде трамплин на этом ронял процесс (`out!`);
    /// теперь регистрация отказывает, как отказывает всякая другая
    /// неисправная регистрация рядом.
    func testTypedOverloadWithoutReturnsValueIsRefusedAtRegistration() {
        let context = Context()
        XCTAssertThrowsError(
            try context.register("silent", flags: [.pure]) { (_: Double) -> Double in 0 }
        ) { error in
            XCTAssertEqual((error as? ChupaScript.Error)?.code, .usage)
        }
    }

    /// Тот же отказ на нуль-арной перегрузке: проверка стоит на каждой из
    /// пяти, а не на одной из них.
    func testTypedNullaryOverloadWithoutReturnsValueIsRefusedToo() {
        let context = Context()
        XCTAssertThrowsError(
            try context.register("silent", flags: []) { () -> Double in 0 }
        )
    }

    /// Сырая перегрузка — единственная, которой Void разрешён: она получает
    /// `out` как есть и сама решает, что с ним делать.
    func testRawOverloadStillAcceptsAVoidFunction() throws {
        let context = Context()
        var called = false
        try context.register("note", minArgs: 0, maxArgs: 0, flags: []) { _, _, out in
            XCTAssertNil(out)
            called = true
        }
        try context.run(context.compile(script: "note();"))
        XCTAssertTrue(called)
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

    /// Сырая перегрузка — единственный способ выразить переменную арность:
    /// `CHUPA_VARIADIC` как `maxArgs`, число аргументов известно только
    /// внутри тела.
    func testRawRegisterHandlesAVariadicFunction() throws {
        let context = Context()
        try context.register("sumAll", minArgs: 1, maxArgs: UInt8(CHUPA_VARIADIC)) { args, ctx, out in
            var total = 0.0
            for i in 0..<args.count {
                guard let value = Double.fromChupa(args[i]) else {
                    throw Error(code: .type, message: "sumAll: аргумент \(i + 1) не Double", offset: nil)
                }
                total += value
            }
            _ = total.intoChupa(ctx, out!)
        }
        let one: ChupaScript.Expression<Double> = try context.compile(expression: "sumAll(3)")
        XCTAssertEqual(try one.eval(), 3)
        let five: ChupaScript.Expression<Double> = try context.compile(expression: "sumAll(1, 2, 3, 4, 5)")
        XCTAssertEqual(try five.eval(), 15)
    }

    /// Сырая перегрузка — единственный способ принять агрегат: массив не
    /// ложится ни в одну маску `CSConvertible`. Проверяет, что агрегатный
    /// аргумент доезжает до Swift-колбэка живым, а не только то, что
    /// компилируется.
    func testRawRegisterReceivesAnAggregateArgumentAlive() throws {
        let context = Context()
        try context.register("lengthOf", minArgs: 1, maxArgs: 1) { args, ctx, out in
            var value = args[0]
            guard chupa_value_kind(&value) == CHUPA_KIND_ARRAY else {
                throw Error(code: .type, message: "lengthOf: аргумент 1 не массив", offset: nil)
            }
            let count = Double(chupa_array_count(&value))
            _ = count.intoChupa(ctx, out!)
        }
        try context.set("xs", text: "[1, 2, 3, 4]")
        let expression: ChupaScript.Expression<Double> = try context.compile(expression: "lengthOf(xs)")
        XCTAssertEqual(try expression.eval(), 4)
    }

    /// `.unrepresentable`, брошенная телом хост-функции, доезжает до
    /// прикладного кода как `.type`, а не сваливается в общий `.host`: смысл
    /// случая — «значение есть, но нужного типа не собрать», то есть ровно
    /// про тип, и в C для этого есть подходящий код.
    func testUnrepresentableFromHostBodyBecomesType() throws {
        let context = Context()
        try context.register("bad") { (_: Double) -> Double in
            throw Error(code: .unrepresentable, message: "not representable", offset: nil)
        }
        let expression: ChupaScript.Expression<Double> = try context.compile(expression: "bad(1)")
        XCTAssertThrowsError(try expression.eval()) { error in
            guard let chupaError = error as? Error else {
                return XCTFail("expected ChupaScript.Error, got \(error)")
            }
            XCTAssertEqual(chupaError.code, .type)
        }
    }
}
