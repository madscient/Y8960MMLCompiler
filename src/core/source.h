#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "device.h"
#include "diag.h"
#include "opcodes.h"
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

// A record #voice or #wave built, and the line it was built on. A #voice is
// packed by then, and `format` says which chunk carries it; a #wave has one
// layout and leaves `format` alone.
struct RecordDef {
    VoiceRecord record{};
    VoiceFormat format = VoiceFormat::Opl;
    int line = 0;
};

// AR, DR, SL and RR, in the order and the bytes chunk 04 carries them: each
// rate is frames in bit7-4 and a step in bit3-0.
using EnvRecord = std::array<std::uint8_t, kEnvValues>;

// How #env writes the four: MUSICA takes the 0-32 rates ENV COPY and MuSICA
// take, RAW takes chunk 04's bytes.
enum class EnvFormat { Musica, Raw };

constexpr int kEnvRateMax = 32;   // MUSICA's rates
constexpr int kEnvLevelMax = 15;  // SL, in either format

const char* envFormatSymbol(EnvFormat f);
bool parseEnvFormat(const std::string& text, EnvFormat& out);
std::vector<std::string> envFormatNames();
// A MUSICA rate as chunk 04's byte, and a byte back to the rate it is.
// False when the value is past 32 or the byte is not one of the 33.
bool envRateByte(int rate, std::uint8_t& out);
bool envRateOf(std::uint8_t byte, int& out);
// Whether a byte is a rate chunk 04 may hold: both halves 1-15.
bool envRateValid(std::uint8_t byte);

struct EnvDef {
    EnvRecord values{};
    int line = 0;
};

// What chunk 80 carries, from #pitch, #title and #author. A line of 0 is an
// item the source does not give.
struct MetaText {
    std::string text;
    int line = 0;
};

struct MetaInfo {
    int pitch = 0;  // A4 in tenths of a hertz
    int pitchLine = 0;
    MetaText title;
    MetaText author;
};

// Chunk 80's item numbers, and the ranges bytecode.md gives them.
constexpr std::uint8_t kMetaPitch = 0x01;
constexpr std::uint8_t kMetaVolume = 0x02;  // master volume; no command writes it
constexpr std::uint8_t kMetaTitle = 0x03;
constexpr std::uint8_t kMetaAuthor = 0x04;
constexpr int kPitchMin = 4300;    // 430.0 Hz
constexpr int kPitchMax = 4500;    // 450.0 Hz
constexpr int kMetaTextMax = 255;  // a value's length is one byte
// A string is ASCII 20h-7Eh alone: what sits above that depends on the machine.
constexpr unsigned char kMetaCharMin = 0x20;
constexpr unsigned char kMetaCharMax = 0x7E;

struct SourceFile {
    std::string path;
    MetaInfo meta;
    std::array<TrackSource, kTrackCount> tracks;
    std::map<std::string, Macro> macros;

    // @128-@191 on the FM family, @16-@31 on the SCC. The presets below those
    // come from the table the compiler carries.
    std::map<int, RecordDef> userVoices;
    std::map<int, RecordDef> userWaves;
    std::map<int, EnvDef> envelopes;  // #env, 1-31

    std::string pcmBankPath;  // empty when the source has no #pcmbank
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
