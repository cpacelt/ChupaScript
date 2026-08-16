// swift-tools-version: 5.9
import PackageDescription

// The C++ engine lives in `core/` and is built by CMake for the CLI, the
// GoogleTest suite and the benchmarks. SwiftPM compiles the same sources
// directly rather than linking a prebuilt archive: one source of truth, and a
// `swift test` run needs no CMake step.
//
// `core/include` is the target's public headers directory, so the module map
// SwiftPM generates for ChupaScriptC covers exactly the public C API and
// nothing else. Everything under `core/src` — including the .hpp files — stays
// private to the target.
let package = Package(
    name: "ChupaScript",
    // Планку задаёт Swift-обвязка, а не движок: C++ ядро от версии платформы
    // не зависит. Преобразования double <-> строка идут через вендоренную
    // double-conversion, а не через <charconv>, чьи плавающие перегрузки
    // libc++ от Apple размечает аннотациями доступности (to_chars — с iOS 16.3,
    // from_chars — с iOS 19.0). Подробности:
    // third_party/double-conversion/VENDORING.md
    platforms: [
        .iOS("15.6"),
        .macOS("12.4"),
    ],
    products: [
        .library(name: "ChupaScript", targets: ["ChupaScript"]),
    ],
    targets: [
        // Вендоренная зависимость: преобразования double <-> строка.
        // Отдельная цель, а не файлы внутри ChupaScriptC, чтобы наши настройки
        // сборки на чужой код не распространялись. Подробности — в
        // third_party/double-conversion/VENDORING.md.
        .target(
            name: "DoubleConversion",
            path: "third_party/double-conversion",
            exclude: ["LICENSE", "VENDORING.md", "CMakeLists.txt"],
            sources: ["double-conversion"],
            // Корень поиска — каталог цели, а не каталог библиотеки: внутри неё
            // инклюды написаны как #include "double-conversion/utils.h".
            publicHeadersPath: "."
        ),
        .target(
            name: "ChupaScriptC",
            dependencies: ["DoubleConversion"],
            path: "core",
            exclude: ["CMakeLists.txt", "tests"],
            sources: ["src"],
            publicHeadersPath: "include",
            cxxSettings: [
                // Matches CMakeLists.txt: only the C API is exported.
                //
                // ВНИМАНИЕ, прежде чем трогать. Из-за .unsafeFlags от этого
                // пакета нельзя зависеть по версии — SwiftPM отвергает такую
                // зависимость, причём проверяет всё замыкание продукта, так что
                // расположение флага на зависимости не спасает. Библиотека
                // подключается исходниками внутрь проекта, и путь `path:` от
                // запрета свободен, поэтому флаг оставлен сознательно.
                // Подробности и цена обратного решения — B43 и B45 в
                // docs/backlog.md.
                .unsafeFlags(["-fvisibility=hidden"], .when(configuration: .release)),
            ]
        ),
        .target(
            name: "ChupaScript",
            dependencies: ["ChupaScriptC"],
            path: "Sources/ChupaScript"
        ),
        .testTarget(
            name: "ChupaScriptTests",
            dependencies: ["ChupaScript"],
            path: "Tests/ChupaScriptTests"
        ),
    ],
    cxxLanguageStandard: .cxx17
)
