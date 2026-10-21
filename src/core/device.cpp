#include "device.h"

#include <algorithm>
#include <cctype>

namespace y8 {
namespace {

struct Entry {
    const char* symbol;
    int lastMelodyChannel;  // the highest channel that is not ADPCM or rhythm
    bool hasAdpcm;
    bool hasRhythm;
};

// Indexed by device number.
const Entry kTable[kDeviceCount] = {
    {"SSGS", 5, false, false},
    {"OPLLEX", 8, false, true},
    {"OPL2EX", 8, true, true},
    {"SCC", 4, false, false},
    {"OPM", 7, false, false},
};

} // namespace

const char* deviceSymbol(Device d) { return kTable[d].symbol; }

Family deviceFamily(Device d) {
    return (d == DevOPLLEX || d == DevOPL2EX || d == DevOPM) ? Family::Fm : Family::Psg;
}

bool deviceHasChannel(Device d, int channel) {
    const Entry& e = kTable[d];
    if (channel < 0) return false;
    if (channel <= e.lastMelodyChannel) return true;
    if (channel == kAdpcmChannel) return e.hasAdpcm;
    if (channel == kRhythmChannel) return e.hasRhythm;
    return false;
}

Dialect dialectFor(Device d, int channel) {
    if (channel == kRhythmChannel && kTable[d].hasRhythm) return Dialect::Rhythm;
    if (channel == kAdpcmChannel && kTable[d].hasAdpcm) return Dialect::Adpcm;
    return deviceFamily(d) == Family::Fm ? Dialect::FmMelody : Dialect::PsgMelody;
}

bool parseDevice(const std::string& text, Device& out) {
    if (text.size() == 1 && text[0] >= '0' && text[0] < '0' + kDeviceCount) {
        out = static_cast<Device>(text[0] - '0');
        return true;
    }
    std::string upper;
    upper.reserve(text.size());
    for (char c : text) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    for (int i = 0; i < kDeviceCount; ++i) {
        if (upper == kTable[i].symbol) {
            out = static_cast<Device>(i);
            return true;
        }
    }
    return false;
}

} // namespace y8
