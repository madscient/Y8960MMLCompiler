add_executable(y8mmlc main.cpp)
target_link_libraries(y8mmlc PRIVATE y8mmlc_core)

# The manifest sets the process code page to UTF-8. The linker merges it into
# the one it generates.
if(WIN32)
    target_sources(y8mmlc PRIVATE y8mmlc.manifest)
endif()

# Handed out on its own, so the C++ runtime goes inside rather than depending on
# the libstdc++ of whatever distribution runs it.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT APPLE)
    target_link_options(y8mmlc PRIVATE -static-libstdc++ -static-libgcc)
endif()
