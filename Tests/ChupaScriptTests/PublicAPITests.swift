// Плоский `import`, а не `@testable`: набор сторожит именно публичную
// поверхность. Под `@testable` внутренние члены видны, и проверка уровня
// доступа стала бы бессмысленной — она бы проходила независимо от того,
// объявлен член публичным или нет.
import XCTest

import ChupaScript

/// То, чем хост пользуется снаружи модуля.
///
/// Остальные наборы импортируют библиотеку через `@testable` и потому не
/// отличают публичное от внутреннего. Здесь проверяется ровно эта разница:
/// каждый случай ниже перестал бы компилироваться, если бы соответствующий
/// член потерял `public`.
final class PublicAPITests: XCTestCase {

    /// Хост, оборачивающий движок в свои типы, обязан уметь сообщить об отказе
    /// тем же типом, что и библиотека.
    ///
    /// Случай не выдуманный: OKBDUI разбирает значения свойств из JSON, и
    /// значение, из которого его тип не собирается, — это ровно
    /// `.unrepresentable`. Без публичного инициализатора хосту пришлось бы
    /// завести свою ошибку о том же самом, и потребитель разбирал бы две.
    func testErrorIsConstructibleOutsideTheModule() {
        let error = ChupaScript.Error(code: .unrepresentable,
                                      message: "'{}' is not a valid Double",
                                      offset: nil)

        XCTAssertEqual(error.code, .unrepresentable)
        XCTAssertEqual(error.message, "'{}' is not a valid Double")
        XCTAssertNil(error.offset)
    }

    /// Смещение доезжает и переживает конструирование на стороне хоста.
    func testErrorCarriesOffsetWhenThereIsOne() {
        let error = ChupaScript.Error(code: .syntax, message: "unexpected end", offset: 3)

        XCTAssertEqual(error.offset, 3)
        XCTAssertEqual(error.description, "syntax at 3: unexpected end")
    }
}
