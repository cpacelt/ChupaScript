# Вендоренная зависимость: преобразования double <-> строка.
# Происхождение, версия и порядок обновления — VENDORING.md рядом.
#
# Цель намеренно НЕ линкует chupascript_compile_options: наш строгий набор
# предупреждений применяется только к нашему коду (см. корневой CMakeLists.txt).
# Заголовки помечены SYSTEM по той же причине — так же, как у GoogleTest.
add_library(double_conversion STATIC
    double-conversion/bignum.cc
    double-conversion/bignum-dtoa.cc
    double-conversion/cached-powers.cc
    double-conversion/double-to-string.cc
    double-conversion/fast-dtoa.cc
    double-conversion/fixed-dtoa.cc
    double-conversion/string-to-double.cc
    double-conversion/strtod.cc
)

# Корень поиска — этот каталог, а не каталог библиотеки: внутри неё инклюды
# написаны как #include "double-conversion/utils.h".
target_include_directories(double_conversion SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(double_conversion PRIVATE cxx_std_17)

# Библиотека попадает внутрь нашей — наружу её символы не выставляем.
set_target_properties(double_conversion PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON)
