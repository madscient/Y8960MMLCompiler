# The core depends on the standard library alone. The CLI is one front end; an
# editor extension talks to that CLI from another repository, and a WebAssembly
# build would take this library as it stands.
add_library(y8mmlc_core STATIC
    adpcm.cpp
    compile.cpp
    device.cpp
    diag.cpp
    json.cpp
    mml.cpp
    source.cpp
    voicedata.cpp
    voiceset.cpp
    writer.cpp
)
target_include_directories(y8mmlc_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
