import XCTest
@testable import ChupaScript

/// Трамплин перерисовки: единственное место, где обвязка получает управление
/// извне.
///
/// Проверять его надо было раньше починки времени жизни, а не после
/// (`docs/backlog.md` B47): убедиться задним числом, что снятие слушателя
/// сработало, нечем — не сработавшее снятие проявляется не отказом, а
/// обращением к освобождённой памяти когда-нибудь потом.
///
/// Со стороны C то же покрыто набором `CApiRedraw` (`core/tests/c_api_test.cpp`),
/// и это не дублирование: там проверяется, что ядро зовёт слушателя, здесь —
/// что вызов доезжает через `Unmanaged` до делегата и что владение по дороге
/// никого не задерживает.
final class RedrawTests: XCTestCase {

    /// Считает уведомления. Ссылку на контекст не держит — иначе тест сам
    /// создал бы цикл, который и собирается ловить.
    private final class Recorder: ContextDelegate {
        private(set) var count = 0
        func contextNeedsRedraw(_ context: Context) { count += 1 }
    }

    // MARK: - Уведомление доезжает

    func testAWriteReachesTheDelegate() throws {
        let context = Context()
        let recorder = Recorder()
        context.delegate = recorder

        try context.set("name", "Вася")

        XCTAssertEqual(recorder.count, 1)
    }

    func testAScriptThatWroteReachesTheDelegate() throws {
        let context = Context()
        try context.set("count", 1)
        let recorder = Recorder()
        context.delegate = recorder

        try context.run(try context.compile(script: "count = 2;"))

        XCTAssertEqual(recorder.count, 1)
    }

    /// Уведомление — про изменение, а не про конец операции.
    ///
    /// Скрипт из одного комментария — не выдуманная форма: ровно так приходит
    /// жест из макета, пока у хостовой функции нет тела. Голое выражение
    /// стейтментом быть не может (`docs/grammar.md` §5), поэтому «выполнился и
    /// ничего не записал» выражается именно так.
    func testAScriptThatWroteNothingDoesNotReachTheDelegate() throws {
        let context = Context()
        try context.set("count", 1)
        let recorder = Recorder()
        context.delegate = recorder

        try context.run(try context.compile(script: "/* reshare('media_topic:1'); */"))

        XCTAssertEqual(recorder.count, 0)
    }

    /// Одно уведомление на операцию, а не на запись.
    func testFourWritesInOneScriptNotifyOnce() throws {
        let context = Context()
        try context.set("a", 0)
        try context.set("b", 0)
        let recorder = Recorder()
        context.delegate = recorder

        try context.run(try context.compile(script: "a = 1;\nb = 2;\na = 3;\nb = 4;"))

        XCTAssertEqual(recorder.count, 1)
    }

    // MARK: - Владение

    func testAClearedDelegateStopsReceiving() throws {
        let context = Context()
        let recorder = Recorder()
        context.delegate = recorder
        try context.set("name", "Вася")

        context.delegate = nil
        try context.set("name", "Петя")

        XCTAssertEqual(recorder.count, 1)
    }

    /// Делегат удерживается слабо: контекст не продлевает ему жизнь.
    ///
    /// Проверяется отпусканием, а не чтением кода: `weak` можно однажды
    /// потерять при правке, и потеря выглядит как утечка чужого объекта, а не
    /// как отказ.
    func testTheDelegateIsHeldWeakly() throws {
        let context = Context()
        weak var weakRecorder: Recorder?

        do {
            let recorder = Recorder()
            weakRecorder = recorder
            context.delegate = recorder
            try context.set("name", "Вася")
            XCTAssertEqual(recorder.count, 1)
        }

        XCTAssertNil(weakRecorder, "контекст задержал делегата")
    }

    /// Умерший делегат не мешает работать дальше.
    func testAWriteAfterTheDelegateDiedIsHarmless() throws {
        let context = Context()
        do {
            let recorder = Recorder()
            context.delegate = recorder
        }

        XCTAssertNoThrow(try context.set("name", "Петя"))
    }

    /// Контекст не удерживает сам себя.
    ///
    /// Трамплин кладёт в ядро `Unmanaged.passUnretained(self)`, и это
    /// единственно возможная половина пары: с `passRetained` контекст стал бы
    /// владельцем самого себя, `deinit` не случился бы никогда, а вместе с ним
    /// не случилось бы и снятие слушателя, которое в нём живёт. Тест сторожит
    /// именно это — подмена одного слова здесь тихо ломает освобождение
    /// ресурсов, и заметить её иначе нечем.
    func testTheContextDoesNotRetainItself() throws {
        weak var weakContext: Context?

        do {
            let context = Context()
            weakContext = context
            try context.set("name", "Вася")
        }

        XCTAssertNil(weakContext, "контекст пережил владельца")
    }

    /// Делегат, отпустивший контекст прямо из уведомления, не роняет вызов.
    ///
    /// Это тот самый путь, из-за которого снятие слушателя обязано жить в
    /// `deinit`, а не полагаться на `chupa_context_destroy`: флаг вычисления к
    /// моменту уведомления уже снят, поэтому уничтожение изнутри колбэка
    /// проходит до конца — прямо посреди кадра операции, которая это
    /// уведомление и породила.
    func testDroppingTheContextFromInsideTheNotificationSurvives() throws {
        final class Dropper: ContextDelegate {
            var context: Context?
            private(set) var count = 0
            func contextNeedsRedraw(_ context: Context) {
                count += 1
                self.context = nil
            }
        }

        let dropper = Dropper()
        weak var weakContext: Context?

        do {
            let context = Context()
            weakContext = context
            context.delegate = dropper
            dropper.context = context
            try context.set("name", "Вася")
        }

        XCTAssertEqual(dropper.count, 1)
        XCTAssertNil(weakContext)
    }
}
