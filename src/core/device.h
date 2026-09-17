#pragma once

#include <cstdint>
#include <string>

namespace y8 {

// The device numbers MASSIGN takes. See bytecode.md, "デバイス番号とチャンネル番号".
enum Device : std::uint8_t {
    DevSSGS = 0,
    DevOPLLEX1 = 1,
    DevOPLLEX2 = 2,
    DevOPL2EX1 = 3,
    DevOPL2EX2 = 4,
    DevDCSG1 = 5,
    DevDCSG2 = 6,
    DevSCC = 7,
    kDeviceCount = 8,
};

// Which of the two event tables a track is written in. It follows from the
// device alone, which is why a track's device has to be known before its MML
// is compiled.
enum class Family { Fm, Psg };

// The four ways a track's MML is read. The channel picks it, not the device:
// one OPL2EX block holds melody, ADPCM and rhythm channels at once.
enum class Dialect {
    FmMelody,
    PsgMelody,
    Adpcm,   // OPL2EX channel 9
    Rhythm,  // OPLLEX and OPL2EX channel 10
};

constexpr int kTrackCount = 16;       // A-P
constexpr int kAdpcmChannel = 9;      // OPL2EX only
constexpr int kRhythmChannel = 10;    // OPLLEX and OPL2EX
constexpr int kRhythmUsesFirst = 6;   // rhythm mode takes channels 6-8
constexpr int kRhythmUsesLast = 8;

const char* deviceSymbol(Device d);
Family deviceFamily(Device d);
bool deviceHasChannel(Device d, int channel);
Dialect dialectFor(Device d, int channel);

// Accepts "OPL2EX1" and "3" alike. Returns false when neither names a device.
bool parseDevice(const std::string& text, Device& out);

} // namespace y8
