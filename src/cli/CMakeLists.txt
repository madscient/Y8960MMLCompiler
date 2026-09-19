add_executable(y8mmlc main.cpp)
target_link_libraries(y8mmlc PRIVATE y8mmlc_core)

# The manifest sets the process code page to UTF-8. The linker merges it into
# the one it generates.
if(WIN32)
    target_sources(y8mmlc PRIVATE y8mmlc.manifest)
endif()
