#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "diag.h"
#include "source.h"

namespace y8 {

constexpr int kPcmPageSize = 256;
constexpr int kPcmPagesMax = 1024;  // the 256KB sample memory
constexpr int kPcmVoiceMax = 64;
constexpr int kPcmRateMin = 1800;
constexpr int kPcmRateMax = 16000;

// One voice file: where it sits in the sample memory and how fast it plays.
// The same seven bytes are chunk 03 of a sequence and one setting of a Y8PC.
struct VoiceFile {
    int number = 0;      // 0-63
    int startPage = 0;
    int pageCount = 0;
    int sampleRate = 0;  // Hz
};

struct AdpcmData {
    std::vector<VoiceFile> files;      // by number, ascending
    std::vector<std::uint8_t> dump;    // the sample memory from page 0
};

// Reads what `src.pcmBankPath` names, relative to the MML source. That is
// either an adpcm_packer JSON with the .bin beside it, or a Y8PC file.
//
// A JSON's entries take voice file numbers 0 upwards in the order they are
// packed, and an #adpcm binding overrides one of those numbers. A Y8PC already
// numbers its voice files, and they are taken as they stand: that is what lets
// y8mmld hand a Y8PC back to the compiler and get the same file out.
bool readAdpcm(const SourceFile& src, AdpcmData& out, Diagnostics& diag);

// The Y8PC of pcmfile.md. `error` says what is wrong when it returns false.
bool isPcmFile(const std::string& bytes);
bool parsePcmFile(const std::string& bytes, AdpcmData& out, std::string& error);

} // namespace y8
