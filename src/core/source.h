#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "device.h"
#include "diag.h"
#include "voicedata.h"

namespace y8 {

// Where a stretch of the track buffer came from, so a diagnostic can point at
// the line the author wrote rather than at an offset in a joined string. A
// line continued with '\' contributes one mark per physical line.
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

struct SampleBinding {
    int number = 0;
    std::string entry;
    int line = 0;
};

// A record #voice or #wave built, and the line it was built on.
struct RecordDef {
    VoiceRecord record{};
    int line = 0;
};

struct SourceFile {
    std::string path;
    std::array<TrackSource, kTrackCount> tracks;
    std::map<std::string, Macro> macros;

    // @128-@191 on the FM family, @16-@31 on the SCC. The presets below those
    // come from the table the compiler carries.
    std::map<int, RecordDef> userVoices;
    std::map<int, RecordDef> userWaves;

    std::string pcmBankJson;  // empty when the source has no #pcmbank
    int pcmBankLine = 0;
    std::vector<SampleBinding> samples;
};

constexpr int kUserVoiceFirst = 128;
constexpr int kUserVoiceLast = 191;
constexpr int kUserWaveFirst = 16;
constexpr int kUserWaveLast = 31;

// Reads the file and sorts its lines. Errors go to `diag`; the parts that did
// read are kept, so one bad line does not hide the rest.
bool readSource(const std::string& path, SourceFile& out, Diagnostics& diag);

// The meta command names the reader accepts, in lower case.
std::vector<std::string> metaCommandNames();

// Splits text into the lines readSource would see. Exposed for the tests.
bool readSourceText(const std::string& path, const std::string& text, SourceFile& out,
                    Diagnostics& diag);

} // namespace y8
