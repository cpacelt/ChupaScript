# ChupaScript core library.
#
# version.cpp is a placeholder so the target has at least one translation unit;
# delete it once real sources land here.
add_library(chupascript STATIC
    src/version.cpp
)

target_include_directories(chupascript PUBLIC include)
target_link_libraries(chupascript PRIVATE chupascript_compile_options)

if(CHUPASCRIPT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
