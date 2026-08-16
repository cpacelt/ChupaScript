# C++ ядро движка и его C API.
#
# Отдельный под, а не сабспек ChupaScript: под — это одна цель сборки, а цель —
# один модуль. Swift-обвязка пишет `import ChupaScriptC`, и модуль с таким
# именем обязан существовать сам по себе. Разделение зеркалит Package.swift, где
# те же исходники живут в цели ChupaScriptC.
Pod::Spec.new do |s|
  s.name             = 'ChupaScriptC'
  s.version          = '0.1.0'
  s.summary          = 'C API of the ChupaScript expression engine'
  s.description      = <<-DESC
The C++ engine behind ChupaScript and its C API. Consumed through the Swift
wrapper (pod `ChupaScript`); host code has no reason to depend on this pod
directly.
                       DESC
  s.homepage         = 'https://github.com/cpacelt/ChupaScript'
  s.license          = { :type => 'Proprietary' }
  s.author           = { 'Roman Putintsev' => 'roman.putincev@ok.ru' }
  s.source           = { :git => 'https://github.com/cpacelt/ChupaScript.git', :tag => s.version.to_s }

  s.ios.deployment_target = '15.6'

  # Наружу торчит ровно один заголовок. Всё остальное — включая .hpp внутри
  # core/src и вендоренную double-conversion — приватно для цели.
  s.public_header_files = 'core/include/chupascript/chupascript.h'

  # third_party/ — вендоренная зависимость (преобразования double <-> строка).
  # Подробности: third_party/double-conversion/VENDORING.md
  s.source_files = [
    'core/src/*.{cpp,hpp}',
    'core/include/chupascript/*.h',
    'third_party/double-conversion/double-conversion/*.{h,cc}'
  ]

  s.preserve_paths = 'core/include/**/*'

  s.libraries = 'c++'

  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CLANG_CXX_LIBRARY' => 'libc++',
    'GCC_C_LANGUAGE_STANDARD' => 'c99',
    # Соответствует CMakeLists.txt и Package.swift: наружу видно только C API,
    # у которого CHUPA_API проставляет visibility("default") явно.
    'OTHER_CFLAGS' => '-fvisibility=hidden',
    'OTHER_CPLUSPLUSFLAGS' => '-fvisibility=hidden',
    'DEFINES_MODULE' => 'YES',
    'HEADER_SEARCH_PATHS' => [
      # c_api.cpp пишет #include "chupascript/chupascript.h", то есть корнем
      # поиска должен быть core/include, а не каталог с самим заголовком.
      '"$(PODS_TARGET_SRCROOT)/core/include"',
      # Внутри double-conversion инклюды написаны как
      # #include "double-conversion/utils.h" — по той же причине корень выше
      # каталога библиотеки.
      '"$(PODS_TARGET_SRCROOT)/third_party/double-conversion"'
    ].join(' ')
  }

  s.module_map = 'ChupaScriptC.modulemap'
end
