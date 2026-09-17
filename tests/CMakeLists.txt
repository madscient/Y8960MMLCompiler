add_executable(mml_test mml_test.cpp)
target_link_libraries(mml_test PRIVATE y8mmlc_core)
add_test(NAME mml COMMAND mml_test)

add_executable(block_test block_test.cpp)
target_link_libraries(block_test PRIVATE y8mmlc_core)
add_test(NAME block COMMAND block_test)
