# The core depends on the standard library alone, so the CLI and a GUI can each
# be replaced without it moving.
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
