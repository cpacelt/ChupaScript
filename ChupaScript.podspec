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
  s.license          = { :type => 'Proprietary' }
  s.author           = { 'Roman Putintsev' => 'roman.putincev@ok.ru' }
  s.source           = { :git => 'https://github.com/cpacelt/ChupaScript.git', :tag => s.version.to_s }

  s.ios.deployment_target = '15.6'
  s.swift_version = '5.0'

  s.source_files = 'Sources/ChupaScript/*.swift'

  s.dependency 'ChupaScriptC', s.version.to_s
end
