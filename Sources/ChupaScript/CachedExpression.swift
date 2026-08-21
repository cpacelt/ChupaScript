import ChupaScriptC

/// Тип, который умеет собраться из уже вычисленного значения.
///
/// Отдельно от `CSValue` (`Sources/ChupaScript/CSValue.swift`), и это не
/// дублирование: `CSValue.chupaEval` принимает **выражение** и сам зовёт
/// типизированный вход C API (`chupa_eval_number` и подобные), а кэшу вход в
/// C ради типа не нужен вовсе — у него на руках уже готовый `ChupaValue`,
/// привезённый одним общим вызовом `chupa_expression_eval_tracked` вместе с
/// зависимостями. Второй вход в C ради того же значения съел бы ровно тот
/// выигрыш, ради которого заведён кэш.
public protocol CSCached {
    /// `nil` — значение оказалось null. Бросает при несовпадении вида.
    static func chupaValue(_ value: ChupaValue) throws -> Self?
}

/// Выражение со снимком: пересчитывает себя, только когда что-то из того, от
/// чего оно зависит, сдвинулось.
///
/// **Снимок живёт здесь, а не в `Expression`.** Одно скомпилированное
/// выражение могут держать два виджета, и кэш у каждого свой (спека §2.5).
///
/// **Сумма, а не массив эпох.** Слагаемые только растут, значит сумма только
/// растёт: выросла хоть одна — сумма строго выросла, и погасить её нечем. XOR
/// на это место не годится, он монотонность не сохраняет: `A=2,B=4` даёт 6, и
/// после `A=3,B=5` снова 6 — два изменения погасили друг друга, а экран
/// застыл бы, ничего не заметив.
///
/// **Складываются всегда все `CHUPA_MAX_DEPS` слов.** Незанятый хвост движок
/// направляет на собственный вечный ноль, поэтому проверка идёт без
/// ветвлений и без чтения счётчика `n`: четыре загрузки и три сложения на
/// каждый кадр, что бы там ни было записано на самом деле.
///
/// **Коробки из набора удерживаются.** `deps[i].owner` — то, внутри чего
/// лежит эпоха; без `chupa_value_retain` адрес эпохи указывал бы внутрь
/// коробки, которую счётчик ссылок вправе освободить, и следующий кадр
/// прочтёт освобождённую память (спека §2.7). У зависимости-ячейки владелец
/// пуст (`CHUPA_KIND_NULL`), и retain/release на нём — no-op по контракту
/// заголовка, поэтому цикл удержания остаётся без ветвлений по виду.
public final class CachedExpression<T: CSCached> {

    private let expression: Expression<T>

    /// Адреса эпох кортежем, а не массивом: массив — это аллокация, ARC и
    /// проверки границ на каждом кадре, а число слагаемых известно на
    /// компиляции (`CHUPA_MAX_DEPS` — часть ABI, задача 8). `nil` — слот не
    /// занят: так лежит и на старте, и после `CHUPA_DEPS_OVERFLOW`.
    private var epochs: (UnsafePointer<UInt64>?, UnsafePointer<UInt64>?,
                          UnsafePointer<UInt64>?, UnsafePointer<UInt64>?) = (nil, nil, nil, nil)

    /// Коробки, за которые держимся, — тем же кортежем и по той же причине.
    /// Отпускаются перед следующим захватом и в `deinit`. Начальное значение —
    /// собственный null движка (`chupa_make_null`), а не что попало: retain на
    /// нём тоже no-op, так что случайный вызов `releaseOwners()` до первого
    /// захвата (которого на деле не бывает — см. `hasValue`) не был бы ошибкой
    /// даже без охранника.
    private var owners: (ChupaValue, ChupaValue, ChupaValue, ChupaValue)

    private var snapshot: UInt64 = 0
    private var cached: T?
    private var hasValue = false

    /// Выражение с некэшируемым вызовом либо со слишком длинным путём: набор
    /// не годится, считаем каждый раз. Флаг ставится один раз, при первом же
    /// захвате, и дальше набор зависимостей не читается вовсе — ни `sum()`,
    /// ни retain/release на него не заглядывают.
    private var uncacheable = false

    /// Сколько раз пришлось войти в движок.
    ///
    /// Читают только тесты пакета (`Tests/`) — им это единственный способ
    /// отличить попадание от промаха снаружи: попадание и промах возвращают
    /// одно и то же значение и различаются лишь тем, входили ли в движок.
    /// Публичным поле остаётся и ради хоста, которому понадобится своя
    /// телеметрия кэша. Бенчмарк задачи 10 сюда не заглядывает: доля
    /// попаданий там считается своим `CacheReader`
    /// (`benchmarks/cache_benchmark.cpp`), который живёт на C++ и об этом
    /// типе не знает.
    public private(set) var missCount = 0

    public init(_ expression: Expression<T>) {
        self.expression = expression
        let null = Self.nullOwner()
        self.owners = (null, null, null, null)
    }

    deinit { releaseOwners() }

    public func value() throws -> T? {
        if hasValue && !uncacheable && sum() == snapshot {
            return cached
        }

        let result = try evalTracked()
        cached = result
        hasValue = true
        return result
    }

    /// Один вход в C: вычисление и захват набора зависимостей разом.
    ///
    /// `out`, `deps` и `n` — временные хранилища на стеке вызова, а не поля:
    /// `withUnsafeTemporaryAllocation` даёт `ChupaValue`/`ChupaDep` домом на
    /// время вызова без malloc/free, ровно как `Context.readError()`
    /// обходится с `ChupaError` — оба типа импортированы из C и не получают
    /// от Swift пустого конструктора для локальной переменной.
    ///
    /// **Порядок «сначала разобрать, потом захватить» обязателен.** `capture`
    /// удерживает новых владельцев и переписывает `epochs`/`snapshot` — если
    /// это сделать до разбора, а `T.chupaValue` бросит (несовпадение вида,
    /// `.unrepresentable`), читатель останется в раздвоенном состоянии:
    /// новый снимок уже на месте, а `cached`/`hasValue` — от старого чтения.
    /// Следующий вызов либо молча отдаст устаревшее значение (снимок
    /// совпадёт, промаха не будет), либо, если это был самый первый захват,
    /// потеряет retain на старых владельцах в `releaseOwners()` — тот
    /// смотрит на `hasValue`, а он ещё `false`. Разбор до захвата убирает оба
    /// исхода разом: бросок — и состояние читателя не тронуто вовсе, как
    /// будто входа в C не было. Значение в `out` борроwed до следующей
    /// операции над контекстом (`chupascript.h`, правило 1), а между вызовом
    /// и разбором контекст не трогается ничем, так что читать его до захвата
    /// зависимостей законно.
    private func evalTracked() throws -> T? {
        try withUnsafeTemporaryAllocation(of: ChupaValue.self, capacity: 1) { outBuf in
            try withUnsafeTemporaryAllocation(of: ChupaDep.self, capacity: Int(CHUPA_MAX_DEPS)) { depsBuf in
                var n: UInt32 = 0
                missCount += 1
                guard chupa_expression_eval_tracked(expression.context.handle,
                                                     expression.handle,
                                                     outBuf.baseAddress!,
                                                     depsBuf.baseAddress!,
                                                     &n)
                else { throw expression.context.makeError() }

                let result = try T.chupaValue(outBuf[0])
                capture(deps: depsBuf, n: n)
                return result
            }
        }
    }

    /// Захватывает новый набор зависимостей взамен старого.
    ///
    /// Порядок обязателен: сперва отпустить прежние коробки, потом решить,
    /// держит ли читатель что-то новое. Так симметрия «захват — retain,
    /// следующий захват — release» не ломается ни на одном исходе, включая
    /// переход в `.uncacheable` и обратно.
    private func capture(deps: UnsafeMutableBufferPointer<ChupaDep>, n: UInt32) {
        releaseOwners()

        uncacheable = (n == CHUPA_DEPS_OVERFLOW)
        guard !uncacheable else {
            // Контракт заголовка: при переполнении каждый deps[i].epoch == NULL,
            // так что epochs можно было бы просто скопировать и отсюда — но
            // явный сброс читается прямее и не зависит от того, что движок
            // обещал заполнить и в этом случае тоже.
            epochs = (nil, nil, nil, nil)
            let null = Self.nullOwner()
            owners = (null, null, null, null)
            return
        }

        // retain на скаляре и на пустом (CHUPA_KIND_NULL) владельце — no-op,
        // поэтому цикл не ветвится по виду зависимости: ровно то же
        // рассуждение, что у самого движка при заполнении набора.
        for dep in deps {
            withUnsafePointer(to: dep.owner) { chupa_value_retain($0) }
        }
        epochs = (deps[0].epoch, deps[1].epoch, deps[2].epoch, deps[3].epoch)
        owners = (deps[0].owner, deps[1].owner, deps[2].owner, deps[3].owner)
        snapshot = sum()
    }

    /// Отпускает текущие коробки. Не читает `uncacheable` дважды —
    /// `capture` зовёт это до того, как переставит флаг, поэтому здесь он ещё
    /// описывает набор, который держим прямо сейчас.
    private func releaseOwners() {
        guard hasValue && !uncacheable else { return }
        withUnsafePointer(to: owners.0) { chupa_value_release($0) }
        withUnsafePointer(to: owners.1) { chupa_value_release($0) }
        withUnsafePointer(to: owners.2) { chupa_value_release($0) }
        withUnsafePointer(to: owners.3) { chupa_value_release($0) }
    }

    /// Четыре загрузки и три сложения, без ветвлений и без чтения счётчика.
    /// `&+` намеренно: переполнение шестидесяти четырёх бит здесь — не
    /// ошибка, а событие, до которого нужно 2^64 инкрементов одной эпохи.
    private func sum() -> UInt64 {
        epochs.0!.pointee &+ epochs.1!.pointee &+ epochs.2!.pointee &+ epochs.3!.pointee
    }

    /// Собственный null движка, а не сырые нулевые байты: представление
    /// `ChupaValue` — деталь движка, и воспроизводить его руками на стороне
    /// Swift было бы ровно тем самым «вторым механизмом там, где хватает
    /// одного», от которого предостерегает заголовок про `CHUPA_MAX_DEPS`.
    private static func nullOwner() -> ChupaValue {
        withUnsafeTemporaryAllocation(of: ChupaValue.self, capacity: 1) { buf in
            chupa_make_null(buf.baseAddress!)
            return buf[0]
        }
    }
}

// MARK: - Базовые типы

extension Double: CSCached {
    public static func chupaValue(_ value: ChupaValue) throws -> Double? {
        var v = value
        switch chupa_value_kind(&v) {
        case CHUPA_KIND_NULL:   return nil
        case CHUPA_KIND_NUMBER: return chupa_value_number(&v)
        default: throw Error(code: .type, message: "value is not a number", offset: nil)
        }
    }
}

extension Bool: CSCached {
    public static func chupaValue(_ value: ChupaValue) throws -> Bool? {
        var v = value
        switch chupa_value_kind(&v) {
        case CHUPA_KIND_NULL: return nil
        case CHUPA_KIND_BOOL: return chupa_value_bool(&v)
        default: throw Error(code: .type, message: "value is not a boolean", offset: nil)
        }
    }
}

extension String: CSCached {

    /// Байты берутся у значения, а не у контекста, и это важно: у кэша на
    /// руках уже готовый `ChupaValue`, и лишний вход в C ради тех же байт
    /// съел бы ровно тот выигрыш, ради которого всё затевалось.
    ///
    /// Всё чтение — внутри `withUnsafePointer`, как в `CSConvertible.fromChupa`
    /// (`HostFunction.swift`), а не `var v = value; chupa_value_string(&v, …)`:
    /// у строки короче шестнадцати байт байты лежат ВНУТРИ самого значения
    /// (SSO), и адрес временной, которую отдаёт `&v`, гарантированно жив
    /// только на время самого вызова — `String` обязан собраться до выхода
    /// из замыкания, пока указатель ещё действителен.
    ///
    /// Кодировка не проверяется — то же решение и по той же причине, что в
    /// `UTF8.swift`: ревалидация была тремя четвертями цены чтения длинной
    /// строки, а всё, что попадает в движок, пришло из `String.utf8`.
    public static func chupaValue(_ value: ChupaValue) throws -> String? {
        try withUnsafePointer(to: value) { v -> String? in
            switch chupa_value_kind(v) {
            case CHUPA_KIND_NULL:
                return nil
            case CHUPA_KIND_STRING:
                var bytes: UnsafePointer<CChar>?
                var length = 0
                chupa_value_string(v, &bytes, &length)
                return String.chupaFromValidUTF8(bytes, count: length)
            default:
                throw Error(code: .type, message: "value is not a string", offset: nil)
            }
        }
    }
}

extension CSCached where Self: RawRepresentable, Self.RawValue: CSCached {

    /// Умолчание, а не вторая перегрузка, — ровно по той причине, что
    /// разобрана у `CSValue`: две перегрузки с одинаковой сигнатурой ломались
    /// от чужого ретроактивного конформанса `String: RawRepresentable`.
    public static func chupaValue(_ value: ChupaValue) throws -> Self? {
        guard let raw = try RawValue.chupaValue(value) else { return nil }
        guard let wrapped = Self(rawValue: raw) else {
            throw Error(code: .unrepresentable,
                        message: "'\(raw)' is not a valid \(Self.self)",
                        offset: nil)
        }
        return wrapped
    }
}
