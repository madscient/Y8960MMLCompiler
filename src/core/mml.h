#pragma once

#include <array>
#include <cstdint>
#include <set>
#include <vector>

#include "device.h"
#include "diag.h"
#include "source.h"
#include "voiceset.h"

namespace y8 {

struct TrackCode {
    bool assigned = false;
    Device device = DevSSGS;
    int channel = 0;
    std::vector<std::uint8_t> bytes;  // the event stream, terminator included
};

struct Sequence {
    std::array<TrackCode, kTrackCount> tracks;
    VoiceSet voices;
    // The voice files the ADPCM tracks sound, which is what chunk 03 carries.
    std::set<int> adpcmVoiceFiles;
};

// Compiles every assigned track of `src`. Tracks are taken in order A to P, so
// the voice slots come out the same for the same source.
bool compileSequence(const SourceFile& src, Sequence& out, Diagnostics& diag);

} // namespace y8
