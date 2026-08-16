Pod::Spec.new do |s|
  s.name             = 'ChupaScript'
  s.version          = '0.1.0'
  s.summary          = 'ChupaScript expression engine for backend-driven UI'
  s.description      = <<-DESC
A compact expression language engine with C API, designed for backend-driven UI
systems. Replaces verbose JSON expression DSLs with a clean, composable syntax.
                       DESC
  s.homepage         = 'https://github.com/cpacelt/ChupaScript'
  s.license          = { :type => 'Proprietary' }
  s.author           = { 'Roman Putintsev' => 'roman.putincev@ok.ru' }
  s.source           = { :git => 'https://github.com/cpacelt/ChupaScript.git', :tag => s.version.to_s }

  s.ios.deployment_target = '15.6'
  s.swift_version = '5.0'

  # C header — compiled as module ChupaScriptC
  s.public_header_files = 'core/include/chupascript/chupascript.h'

  # C++ engine sources
  #
  # third_party/ — вендоренная зависимость (преобразования double <-> строка).
  # Её заголовки в public_header_files не попадают, наружу торчит только
  # chupascript.h. Подробности: third_party/double-conversion/VENDORING.md
  s.source_files = [
    'core/src/*.{cpp,hpp}',
    'core/include/chupascript/*.h',
    'third_party/double-conversion/double-conversion/*.{h,cc}',
    'Sources/ChupaScript/*.swift'
  ]

  # Preserve the header directory structure
  s.preserve_paths = 'core/include/**/*'

  # C++17, hidden visibility for non-API symbols
  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'GCC_C_LANGUAGE_STANDARD' => 'c99',
    'CLANG_CXX_LIBRARY' => 'libc++',
    'OTHER_CFLAGS' => '-fvisibility=hidden',
    'OTHER_CPLUSPLUSFLAGS' => '-fvisibility=hidden',
    'DEFINES_MODULE' => 'YES',
    'SWIFT_OBJC_INTERFACE_HEADER_NAME' => 'ChupaScript-Swift.h',
    # Внутри double-conversion инклюды написаны как
    # #include "double-conversion/utils.h", поэтому корнем поиска должен быть
    # каталог, содержащий каталог библиотеки.
    'HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/third_party/double-conversion"'
  }

  # Custom modulemap: ChupaScriptC wraps the C header, ChupaScript wraps Swift
  s.module_map = 'ChupaScript.modulemap'
end
