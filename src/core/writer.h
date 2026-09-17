#pragma once

#include <cstdint>
#include <vector>

#include "adpcm.h"
#include "mml.h"

namespace y8 {

// The Y8SQ block: the header, then the chunks. What MSAVE writes and MLOAD
// reads. See bytecode.md, "ブロック".
std::vector<std::uint8_t> writeBlock(const Sequence& seq, const AdpcmData& adpcm);

// The Y8PC file: the header, the settings, then the sample memory dump. What
// EXPORT PCM writes and IMPORT PCM reads. See pcmfile.md.
std::vector<std::uint8_t> writePcmFile(const AdpcmData& adpcm);

} // namespace y8
