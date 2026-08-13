# ChupaScript: Swift-обёртки

Дата: 2026-08-13
Статус: дизайн утверждён

## 1. Область документа

Граница между C API ChupaScript и Swift-обёртками, которые потребляет OKBDUI.
Документ описывает публичные Swift-типы, их ответственность, время жизни и
паттерны использования. C API контракт (`chupascript.h`) описан здесь только
в части, влияющей на обёртки; полное нормативное описание — в
`2026-08-10-chupascript-c-api-design.md`.

## 2. Контекст

OKBDUI заменяет многословную систему Expression/Variable/VariablesStorage на
ChupaScript. Текущая система (MapLink, FormattedLink, InterpolatedString,
VariableLink, VariablesStorage) удаляется. Вместо неё — ChupaContext,
ChupaExpression и типизированные eval-методы.

ChupaScript поставляется как CocoaPods pod, компилируемый из исходников: C++
движок + C заголовок + Swift-обёртки. Без xcframework, без бинарников.

## 3. Архитектура

```
┌─────────────────────────────────────────┐
│  OKBDUI (потребитель)                   │
│  WidgetProperty<T>, WidgetStateContext  │
├─────────────────────────────────────────┤
│  ChupaScript Swift-обёртки (публичные)  │
│  ChupaContext, ChupaExpression,         │
│  ChupaScript, ChupaError                │
│  ChupaContextDelegate                   │
├─────────────────────────────────────────┤
│  C API (chupascript.h)                  │
├─────────────────────────────────────────┤
│  C++ движок (core/)                     │
└─────────────────────────────────────────┘
```

Swift-обёртки живут в репо ChupaScript в директории `swift/`. Podspec
включает C++ исходники, C заголовок и Swift-файлы. Modulemap генерируется
CocoaPods автоматически.

## 4. C API контракт

Контракт из `2026-08-10-chupascript-c-api-design.md` с изменениями:

### 4.1. Что изменилось

> **Эта спека — ревизия C API** (`2026-08-10-chupascript-c-api-design.md`).
> Пункты 1–5 ниже меняют состав заголовка; C API спека должна быть обновлена
> соответственно. Пока она не обновлена, настоящий документ — единственный
> источник истины для перечисленных функций.

1. **Добавлены** типизированные setter'ы для скаляров — `chupa_context_set_bool`,
   `chupa_context_set_number`, `chupa_context_set_string`. Заменяют
   `chupa_context_set_value` + `chupa_value_*` фабрики.
2. **Добавлен** `chupa_context_on_redraw` — нотификация хоста о перерисовке.
3. **Убран** `chupa_context_set_value` — не нужен, скаляры ставятся напрямую.
4. **Убран** `ChupaValue` как тип — ни struct, ни opaque pointer. Нет в
   публичном API. C++ `CS::Value` остаётся внутренним типом движка.
5. **Убраны** `chupa_value_*` фабрики и accessor'ы — нет типа, нет фабрик.
6. `chupa_eval` (generic), `chupa_value_kind`, `chupa_array_*`, `chupa_object_*`
   из спеки — оставлены для будущего (чтение массивов/объектов из eval). Пока
   не реализуются.
7. `batch_begin`/`batch_commit`, `is_dirty` — внутреннее движка, не в C API.
8. **Переименована** `chupascript_version` → `chupa_version` — приведение к
   единому префиксу `chupa_`. Текущий заголовок-заглушка содержит старое имя;
   переименование — часть реализации этого контракта.

### 4.2. Итоговый заголовок

```c
#ifndef CHUPASCRIPT_H
#define CHUPASCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Version macros — used by chupa_version() and available to hosts. */
#define CHUPASCRIPT_VERSION_MAJOR 0
#define CHUPASCRIPT_VERSION_MINOR 1
#define CHUPASCRIPT_VERSION_PATCH 0

#if defined(__clang__)
#  define CHUPA_NONNULL_BEGIN _Pragma("clang assume_nonnull begin")
#  define CHUPA_NONNULL_END   _Pragma("clang assume_nonnull end")
#  define CHUPA_NULLABLE      _Nullable
#else
#  define CHUPA_NONNULL_BEGIN
#  define CHUPA_NONNULL_END
#  define CHUPA_NULLABLE
#endif

#define CHUPA_API __attribute__((visibility("default")))

#if defined(__GNUC__) || defined(__clang__)
#  define CHUPA_MUST_USE __attribute__((warn_unused_result))
#else
#  define CHUPA_MUST_USE
#endif

#ifdef __cplusplus
extern "C" {
#endif

CHUPA_NONNULL_BEGIN

typedef struct ChupaContext    ChupaContext;
typedef struct ChupaExpression ChupaExpression;
typedef struct ChupaScript     ChupaScript;

typedef enum ChupaKind {
    CHUPA_KIND_NULL   = 0,
    CHUPA_KIND_BOOL   = 1,
    CHUPA_KIND_NUMBER = 2,
    CHUPA_KIND_STRING = 3,
    CHUPA_KIND_ARRAY  = 4,
    CHUPA_KIND_OBJECT = 5
} ChupaKind;

typedef enum ChupaStatus {
    CHUPA_OK    = 0,  /* значение получено          */
    CHUPA_NULL  = 1,  /* выражение дало null        */
    CHUPA_ERROR = 2   /* ошибка либо не тот тип     */
} ChupaStatus;

typedef enum ChupaErrorCode {
    CHUPA_ERR_NONE = 0,
    CHUPA_ERR_SYNTAX,
    CHUPA_ERR_NAME,
    CHUPA_ERR_TYPE,
    CHUPA_ERR_RANGE,
    CHUPA_ERR_DATA,
    CHUPA_ERR_USAGE,
    CHUPA_ERR_MEMORY
} ChupaErrorCode;

/* ─── Версия ─── */

CHUPA_API const char *chupa_version(void);

/* ─── Контекст ─── */

CHUPA_API ChupaContext *CHUPA_NULLABLE chupa_context_create(void);
CHUPA_API void chupa_context_destroy(ChupaContext *CHUPA_NULLABLE ctx);

/* Установка корня из текста-литерала ChupaScript (не JSON). */
CHUPA_API bool chupa_context_set(ChupaContext *ctx,
                                 const char *name, size_t name_len,
                                 const char *text, size_t text_len);

/* Установка скалярных корней напрямую. */
CHUPA_API void chupa_context_set_bool  (ChupaContext *ctx,
                                        const char *name, size_t name_len,
                                        bool value);
CHUPA_API void chupa_context_set_number(ChupaContext *ctx,
                                        const char *name, size_t name_len,
                                        double value);
CHUPA_API void chupa_context_set_string(ChupaContext *ctx,
                                        const char *name, size_t name_len,
                                        const char *text, size_t text_len);

/* ─── Redraw нотификация ─── */

typedef void (*ChupaRedrawListener)(ChupaContext *ctx, void *CHUPA_NULLABLE user_data);

CHUPA_API void chupa_context_on_redraw(ChupaContext *ctx,
                                       ChupaRedrawListener listener,
                                       void *CHUPA_NULLABLE user_data);

/* ─── Компиляция ─── */

CHUPA_API ChupaExpression *CHUPA_NULLABLE
chupa_compile_expression(ChupaContext *ctx, const char *source, size_t len);

CHUPA_API ChupaScript *CHUPA_NULLABLE
chupa_compile_script(ChupaContext *ctx, const char *source, size_t len);

/* ─── Вычисление ─── */

CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_number(ChupaContext *ctx, ChupaExpression *e, double *out);

CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_bool(ChupaContext *ctx, ChupaExpression *e, bool *out);

CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_string(ChupaContext *ctx, ChupaExpression *e,
                  const char *CHUPA_NULLABLE *CHUPA_NULLABLE out, size_t *len);

/* ─── Исполнение скрипта ─── */

CHUPA_API CHUPA_MUST_USE bool chupa_run(ChupaContext *ctx, ChupaScript *script);

/* ─── Ошибки ─── */

CHUPA_API ChupaErrorCode chupa_context_error_code  (const ChupaContext *ctx);
CHUPA_API size_t         chupa_context_error_offset(const ChupaContext *ctx);
CHUPA_API const char *CHUPA_NULLABLE
chupa_context_error(const ChupaContext *ctx, size_t *CHUPA_NULLABLE len);

CHUPA_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* CHUPASCRIPT_H */
```

## 5. Swift-обёртки

### 5.1. ChupaContext

`final class`, владеет C-хендлом. `deinit` вызывает `chupa_context_destroy`.

```swift
public final class ChupaContext {
    internal let handle: OpaquePointer
    public weak var delegate: ChupaContextDelegate?

    public init() {
        guard let h = chupa_context_create() else {
            fatalError("ChupaContext: allocation failed")
        }
        handle = h
        registerRedrawCallback()
    }

    deinit {
        chupa_context_destroy(handle)
    }
}
```

#### Установка переменных

Текстовый литерал — для серверных данных (JSON разобранный хостом, значение
передаётся как текст литерала ChupaScript):

```swift
@discardableResult
public func set(_ name: String, text: String) -> Bool
```

Скаляры — для программных обновлений (действия, `set_variable`):

```swift
public func set(_ name: String, _ value: Bool)
public func set(_ name: String, _ value: Double)
public func set(_ name: String, _ value: String)
```

#### Компиляция

```swift
public func compile(expression source: String) throws -> ChupaExpression
public func compile(script source: String) throws -> ChupaScript
```

`throws` — ошибка компиляции восстановима (catch → использовать дефолт).

#### Выполнение

```swift
@discardableResult
public func run(_ script: ChupaScript) -> Bool
```

`Bool` — успех/провал. Подробность в `error`. Скрипт может оставить частичные
изменения при ошибке (см. C API спеку §7).

#### Ошибка

```swift
public var error: ChupaError?
```

`nil` — последняя операция успешна. Иначе — код, сообщение, смещение. Ошибка
живёт в контексте, действительна до следующего вызова.

### 5.2. ChupaContextDelegate

Протокол для нотификации хоста о перерисовке. `weak` — без retain cycle.

```swift
public protocol ChupaContextDelegate: AnyObject {
    func contextNeedsRedraw(_ context: ChupaContext)
}
```

Внутри `ChupaContext` — `@convention(c)` трамполин. `Unmanaged.passUnretained(self)`
безопасен: контекст владеет хендлом, `chupa_context_destroy` в `deinit`
гарантирует, что каллбэк не выстрелит после смерти.

```swift
private func registerRedrawCallback() {
    let ptr = Unmanaged.passUnretained(self).toOpaque()
    chupa_context_on_redraw(handle, Self.trampoline, ptr)
}

private static let trampoline: @convention(c) (
    OpaquePointer?, UnsafeMutableRawPointer?
) -> Void = { _, userData in
    guard let userData else { return }
    let ctx = Unmanaged<ChupaContext>.fromOpaque(userData).takeUnretainedValue()
    ctx.delegate?.contextNeedsRedraw(ctx)
}
```

Хост становится делегатом, при необходимости диспатчит на нужный поток:

```swift
extension MyController: ChupaContextDelegate {
    func contextNeedsRedraw(_ context: ChupaContext) {
        DispatchQueue.main.async { [weak self] in
            self?.recalculate()
        }
    }
}
```

### 5.3. ChupaExpression

`final class`, сильная ссылка на контекст. Не может пережить контекст.
Retain cycle нет — контекст не держит выражения.

Сильная ссылка на контекст — **намеренное усиление** Swift-слоя. C API
объявляет хендлы невладеющими (C API спека §3), но в Swift отпускание
контекста при живом выражении привело бы к use-after-free. Ссылка
гарантирует: пока выражение живо, контекст жив. Хост не должен держать
выражения дольше контроллера экрана — это время жизни совпадает, и
усиление не продлевает его на практике.

`deinit` отсутствует: C API не предоставляет функции освобождения для
выражений и скриптов — они принадлежат контексту и умирают вместе с ним.
Добавлять `deinit` некуда и нечего.

```swift
public final class ChupaExpression {
    internal let handle: OpaquePointer
    internal let context: ChupaContext

    public func evalNumber() -> Double?
    public func evalBool() -> Bool?
    public func evalString() -> String?
}
```

`nil` — выражение дало `null` или ошибку. Для дебага — `ctx.error`.

Маппинг `ChupaStatus → Optional`:

| C статус | Swift возврат |
|---|---|
| `CHUPA_OK` | значение (`Double` / `Bool` / `String`) |
| `CHUPA_NULL` | `nil` |
| `CHUPA_ERROR` | `nil`, подробность в `ctx.error` |

Строка возвращается копией (Swift `String` из `UnsafeBufferPointer`), не
указателем в пул контекста — правило времени жизни из C API спеки §7
соблюдено.

### 5.4. ChupaScript

`final class`, сильная ссылка на контекст. Непрозрачный хендл, используется
только в `ctx.run(_:)`.

`deinit` отсутствует по той же причине, что у `ChupaExpression` (§5.3):
C API не предоставляет освобождения для скриптов.

```swift
public final class ChupaScript {
    internal let handle: OpaquePointer
    internal let context: ChupaContext
}
```

### 5.5. ChupaError

```swift
public struct ChupaError: Error, CustomStringConvertible {
    public let code: ChupaErrorCode
    public let message: String
    public let offset: Int

    public var description: String {
        "\(code) at \(offset): \(message)"
    }
}
```

## 6. Чего нет

| Тип/механизм | Почему |
|---|---|
| `ChupaValue` (struct или opaque) | Чтение — типизированные eval-методы. Запись — `set(text:)` и перегрузки `set(_:Bool/Double/String)`. Массивы/объекты — через `set(text:)`. |
| `ChupaEvaluable` протокол | Избыточный слой. Потребитель решает типизацию сам. |
| `batch_begin`/`batch_commit` | Внутреннее движка. |
| `is_dirty` | Внутреннее движка. |
| Color | Забота хоста, не ChupaScript. |
| RawRepresentable eval | В бэклоге. |

## 7. Потоки

Обёртка без `@MainActor` — ChupaScript универсальный (iOS + Android). Хост
вешает `@MainActor` на своей стороне.

`contextNeedsRedraw` вызывается на потоке, где звали `chupa_run`. Хост
диспатчит на main при необходимости.

Один контекст — один поток одновременно (см. C API спека §3). Разные
контексты независимы.

**Освобождение контекста — на том же потоке, на котором он использовался.**
`deinit` вызывает `chupa_context_destroy`, которая unregister'ит redraw-каллбэк.
Если контекст использовался на фоновом потоке, а `deinit` сработал на main
(Swift ARC не гарантирует поток), между `destroy` и каллбэком, который ещё
в полёте на фоновом, возможна гонка. Хост обязан обеспечить освобождение
контекста на потоке последнего использования — например, захватить контекст
в `DispatchQueue.async` на нужном потоке и отпустить там.

## 8. Поставка

Podspec компилирует из исходников:
- `s.source_files` = C++ (`core/src/*.cpp`, `core/src/*.hpp`) + C заголовок
  (`core/include/chupascript/*.h`) + Swift (`swift/*.swift`)
- `s.public_header_files` = `core/include/chupascript/chupascript.h`
- Modulemap генерируется CocoaPods
- Без xcframework, без бинарников

**C++ и Swift в одном target.** CocoaPods поддерживает смешанные target'ы,
но требует явной конфигурации: `s.pod_target_xcconfig` должен включать
`SWIFT_OBJC_INTERFACE_HEADER_NAME` и, при необходимости, bridging-настройки
для доступа Swift к C-заголовку. Modulemap, генерируемый CocoaPods, решает
большую часть — Swift видит C-функции напрямую. Если возникнут проблемы с
взаимодействием C++ → Swift (Swift не видит C++ классы), заголовок уже
C-совместим (`extern "C"`), и необходимости в C++ interop нет.

## 9. Открытые вопросы

- **B29** (backlog) — инкрементальный пересчёт props: движок кэширует и
  тречит dirty внутренне. Не влияет на C API контракт.
- **Чтение массивов/объектов из eval** — `chupa_eval` + `chupa_value_*`
  accessor'ы из C API спеки. Не реализуются пока, но контракт оставлен.
- **`set(text:)` для программных сложных значений** — если понадобится
  программно создать массив/объект, можно передать текст литерала:
  `ctx.set("items", text: "[1, 2, 3]")`. Достаточно для первого этапа.
- **RawRepresentable eval** — отложено. Потребитель (BDUI) сам конвертирует
  строку в rawValue при необходимости.
- **Интеграция с OKBDUI** — отдельная задача. Эта спека описывает только
  ChupaScript Swift-обёртки.
