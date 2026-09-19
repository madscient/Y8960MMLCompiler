add_executable(mml_test mml_test.cpp)
target_link_libraries(mml_test PRIVATE y8mmlc_core)
add_test(NAME mml COMMAND mml_test)

add_executable(block_test block_test.cpp)
target_link_libraries(block_test PRIVATE y8mmlc_core)
add_test(NAME block COMMAND block_test)

add_executable(grammar_test grammar_test.cpp)
target_link_libraries(grammar_test PRIVATE y8mmlc_core)
target_compile_definitions(grammar_test PRIVATE
    Y8MMLC_GRAMMAR_PATH="${PROJECT_SOURCE_DIR}/syntaxes/y8960mml.tmLanguage.json")
add_test(NAME grammar COMMAND grammar_test)

# Runs the executable itself: what lets it take names that are not ASCII on
# Windows is its manifest, which only the real executable carries.
if(TARGET y8mmlc)
    add_executable(cli_test cli_test.cpp)
    target_compile_definitions(cli_test PRIVATE Y8MMLC_EXE="$<TARGET_FILE:y8mmlc>")
    add_dependencies(cli_test y8mmlc)
    add_test(NAME cli COMMAND cli_test)
endif()

# The words y8mmlc prints are English and ASCII: nothing outside a comment in
# src/ may be anything else.
add_executable(ascii_test ascii_test.cpp)
target_compile_definitions(ascii_test PRIVATE Y8MMLC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME ascii COMMAND ascii_test)
