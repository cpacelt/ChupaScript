import XCTest
@testable import ChupaScript

/// Построение строки без проверки кодировки (`String.chupaFromValidUTF8`).
///
/// Путь опирается на подчёркнутый символ стандартной библиотеки, и сторожить
/// его надо двумя разными способами. Первый — что символ вообще разрешается:
/// его исчезновение даёт падение при загрузке образа, а не ошибку компиляции,
/// и любой тест из этого файла упадёт первым. Второй — что результат совпадает
/// с тем, что раньше давал `String(decoding:as: UTF8.self)`: подмена
/// незаметна ни по равенству, ни по длине, ни по хэшу, ни по порядку.
final class ValidUTF8StringTests: XCTestCase {

    /// Строки, на которых проверяется совпадение. Границы взяты не наугад:
    /// пустая — единственный случай с нулевым указателем; 15 и 16 байт — по обе
    /// стороны от порога, за которым `String` перестаёт помещаться в себя;
    /// кириллица и эмодзи — не-ASCII, ради которых всё и затевалось.
    private let samples = [
        "",
        "a",
        "abcdefghijklmno",
        "abcdefghijklmnop",
        "Привет",
        "Привет, мир — раз два три",
        "смайлик 😀 и хвост",
        String(repeating: "x", count: 300),
        String(repeating: "я", count: 300),
    ]

    func testMatchesDecodingByteForByte() {
        for sample in samples {
            var bytes = Array(sample.utf8)
            let made = bytes.withUnsafeMutableBufferPointer { buffer -> String in
                buffer.baseAddress?.withMemoryRebound(to: CChar.self,
                                                      capacity: buffer.count) { start in
                    String.chupaFromValidUTF8(start, count: buffer.count)
                } ?? ""
            }
            XCTAssertEqual(made, sample, "равенство")
            XCTAssertEqual(made.count, sample.count, "число символов")
            XCTAssertEqual(made.utf8.count, sample.utf8.count, "число байт")
            XCTAssertEqual(made.hashValue, sample.hashValue, "хэш")
            XCTAssertEqual(Array(made.unicodeScalars), Array(sample.unicodeScalars),
                           "скаляры")
            XCTAssertEqual(made < "zzz", sample < "zzz", "порядок")
        }
    }

    /// То же сравнение, но мимо `chupaFromValidUTF8`, прямо по подчёркнутому
    /// символу. Без этого теста он на новых системах вообще не вызывался бы:
    /// обёртка там уходит в публичный `String(copying: UTF8Span)`, и
    /// исчезновение символа заметил бы только тот, кто собирает под старую ОС.
    /// Здесь же он вызывается на любом хосте — а раз он `@usableFromInline`,
    /// то и разрешается на любом.
    func testUncheckedSymbolItselfMatchesDecoding() {
        for sample in samples {
            var bytes = Array(sample.utf8)
            let made = bytes.withUnsafeMutableBufferPointer { buffer -> String in
                guard buffer.count > 0 else { return "" }
                return String._uncheckedFromUTF8(UnsafeBufferPointer(buffer),
                                                 isASCII: false)
            }
            XCTAssertEqual(made, sample, "равенство")
            XCTAssertEqual(made.utf8.count, sample.utf8.count, "число байт")
            XCTAssertEqual(made.hashValue, sample.hashValue, "хэш")
            XCTAssertEqual(Array(made.unicodeScalars), Array(sample.unicodeScalars),
                           "скаляры")
        }
    }

    /// Пустая строка приходит с нулевым указателем: у неё нечего показывать в
    /// пуле текста. Отдельный тест потому, что это единственная ветка, где
    /// буфер вовсе не заводится.
    func testEmptyBytesGiveEmptyString() {
        XCTAssertEqual(String.chupaFromValidUTF8(nil, count: 0), "")
        XCTAssertEqual(String.chupaFromValidUTF8(nil, count: 7), "")
    }

    /// Тот же путь, но целиком: значение доходит от хоста через движок обратно
    /// в `String`. Сторожит не помощник, а то, что его подключили туда, куда
    /// нужно, — в чтение результата вычисления.
    func testNonAsciiSurvivesTheWholeRoundTrip() throws {
        let context = Context()
        let value = "Заказ №42 — доставка завтра 😀"
        try context.set("text", value)

        let expression = try context.compile(expression: "text", as: String.self)
        XCTAssertEqual(try expression.eval(), value)

        // И через сборку, а не только через чтение: format режет шаблон по
        // ASCII-маркерам, и склейка не должна разрывать скаляр.
        let built = try context.compile(expression: "format('до ${} после', text)",
                                        as: String.self)
        XCTAssertEqual(try built.eval(), "до \(value) после")
    }

    /// Сообщение об ошибке идёт тем же путём (`Context.makeError`).
    func testErrorMessageIsBuiltThroughTheSamePath() {
        let context = Context()
        XCTAssertThrowsError(try context.compile(expression: "unknownName",
                                                 as: Double.self)) { error in
            let cs = error as? ChupaScript.Error
            XCTAssertEqual(cs?.message.isEmpty, false)
        }
    }
}
