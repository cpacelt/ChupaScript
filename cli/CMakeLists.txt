add_executable(chupascript_repl main.cpp)

target_link_libraries(chupascript_repl PRIVATE
    chupascript
    chupascript_compile_options
)
