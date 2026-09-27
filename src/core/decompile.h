#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "adpcm.h"
#include "diag.h"

namespace y8 {

struct DecompileOptions {
    std::string name;                 // what the diagnostics call the block
    const AdpcmData* pcm = nullptr;   // the Y8PC's contents, when there is one
    std::string pcmBankPath;          // what #pcmbank is to say, relative to the MML
};

// Turns a Y8SQ block back into MML source.
//
// What it aims at is a source that sounds the same, not one that compiles back
// to the same bytes: a length no single note can be written with becomes tied
// notes and rests. Written that way the output is a fixed point - decompiling
// what it compiles to gives the same text again.
//
// Something the MML cannot say is left out with a warning. The return is false,
// with an error, only when the block cannot be read or needs a Y8PC it was not
// given.
bool decompileBlock(const std::vector<std::uint8_t>& block, const DecompileOptions& opt,
                    std::string& mml, Diagnostics& diag);

} // namespace y8
