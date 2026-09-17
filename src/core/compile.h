#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "diag.h"

namespace y8 {

struct CompileResult {
    std::vector<std::uint8_t> sequence;  // the Y8SQ block
    std::vector<std::uint8_t> pcm;       // the Y8PC file, empty without #pcm
    bool hasPcm = false;
};

// Reads one MML source and builds both outputs. Everything it has to say goes
// to `diag`; the return says whether anything may be written.
bool compileFile(const std::string& path, CompileResult& out, Diagnostics& diag);

// The same, from text already in hand. For the tests.
bool compileText(const std::string& path, const std::string& text, CompileResult& out,
                 Diagnostics& diag);

// The MSX-DOS base name an output takes: upper case, at most eight characters.
// `truncated` says whether anything was cut off.
std::string outputBaseName(const std::string& inputPath, bool& truncated);

} // namespace y8
