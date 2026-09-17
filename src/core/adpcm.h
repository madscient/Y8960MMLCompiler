#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "diag.h"
#include "source.h"

namespace y8 {

constexpr int kPcmPageSize = 256;
constexpr int kPcmPagesMax = 1024;  // the 256KB sample memory
constexpr int kPcmVoiceMax = 32;
constexpr int kPcmRateMin = 1800;
constexpr int kPcmRateMax = 16000;

// One voice file: where it sits in the sample memory and how fast it plays.
// The same seven bytes are chunk 03 of a sequence and one setting of a Y8PC.
struct VoiceFile {
    int number = 0;      // 0-31
    int startPage = 0;
    int pageCount = 0;
    int sampleRate = 0;  // Hz
};

struct AdpcmData {
    std::vector<VoiceFile> files;      // by number, ascending
    std::vector<std::uint8_t> dump;    // the sample memory from page 0
};

// Reads the adpcm_packer JSON `src.pcmJson` names, the .bin beside it, and the
// #voice bindings. Paths are taken relative to the MML source.
bool readAdpcm(const SourceFile& src, AdpcmData& out, Diagnostics& diag);

} // namespace y8
