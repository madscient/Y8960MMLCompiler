#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
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
    // The software envelopes the PSG family's tracks name, which is what
    // chunk 04 carries.
    std::map<int, EnvRecord> envelopes;
    MetaInfo meta;  // chunk 80
};

// Compiles every assigned track of `src`. Tracks are taken in order A to P, so
// the voice slots come out the same for the same source.
bool compileSequence(const SourceFile& src, Sequence& out, Diagnostics& diag);

// Whether Y may write `data` to register `reg` of `dev`; `why` says why not.
// y8mmld asks the same question before it writes a Y.
bool regWritable(Device dev, long reg, long data, std::string& why);

} // namespace y8
