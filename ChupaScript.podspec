# Swift-обвязка над движком.
#
# Только Swift: C++ ядро и его C API живут в поде ChupaScriptC, потому что цель
# сборки — это один модуль, а `import ChupaScriptC` требует, чтобы модуль с
# таким именем существовал отдельно. Разделение зеркалит Package.swift.
Pod::Spec.new do |s|
  s.name             = 'ChupaScript'
  s.version          = '0.1.0'
  s.summary          = 'ChupaScript expression engine for backend-driven UI'
  s.description      = <<-DESC
A compact expression language engine with C API, designed for backend-driven UI
systems. Replaces verbose JSON expression DSLs with a clean, composable syntax.
                       DESC
  s.homepage         = 'https://github.com/cpacelt/ChupaScript'
  # NOTICE.md перечисляет вендоренные компоненты и воспроизводит их лицензии.
  # Через :file его текст попадает в acknowledgements, которые CocoaPods
  # собирает интегратору, — иначе о вложенной BSD-3 он не узнает никак
  # (docs/backlog.md B44).
  s.license          = { :type => 'Proprietary', :file => 'NOTICE.md' }
  s.author           = { 'Roman Putintsev' => 'roman.putincev@ok.ru' }
  s.source           = { :git => 'https://github.com/cpacelt/ChupaScript.git', :tag => s.version.to_s }

  s.ios.deployment_target = '15.6'
  # Обвязка собирается в шестом языковом режиме без единого замечания —
  # проверено `swift build -Xswiftc -swift-version -Xswiftc 6`. Прежняя
  # пятёрка перекочевала из первой редакции подспека и не проверялась ни разу
  # (docs/backlog.md B48).
  s.swift_version = '6.0'

  s.source_files = 'Sources/ChupaScript/*.swift'

  s.dependency 'ChupaScriptC', s.version.to_s
end
