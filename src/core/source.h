#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "device.h"
#include "diag.h"

namespace y8 {

// Where a stretch of the track buffer came from, so a diagnostic can point at
// the line the author wrote rather than at an offset in a joined string.
struct OriginMark {
    std::size_t offset;  // into TrackSource::text
    int line;
    int column;
};

struct TrackSource {
    bool assigned = false;
    Device device = DevSSGS;
    int channel = 0;
    int assignLine = 0;

    // Every track line of this track, joined with newlines. The MML reader
    // treats a newline as it treats any other space.
    std::string text;
    std::vector<OriginMark> marks;

    // The line and column the byte at `offset` was written at.
    void locate(std::size_t offset, int& line, int& column) const;
};

struct Macro {
    bool isString = false;
    long number = 0;
    std::string text;
    int line = 0;
};

struct VoiceBinding {
    int number = 0;
    std::string entry;
    int line = 0;
};

struct SourceFile {
    std::string path;
    std::array<TrackSource, kTrackCount> tracks;
    std::map<std::string, Macro> macros;

    std::string pcmJson;  // empty when the source has no #pcm
    int pcmLine = 0;
    std::vector<VoiceBinding> voices;
};

// Reads the file and sorts its lines. Errors go to `diag`; the parts that did
// read are kept, so one bad line does not hide the rest.
bool readSource(const std::string& path, SourceFile& out, Diagnostics& diag);

// Splits text into the lines readSource would see. Exposed for the tests.
bool readSourceText(const std::string& path, const std::string& text, SourceFile& out,
                    Diagnostics& diag);

} // namespace y8
