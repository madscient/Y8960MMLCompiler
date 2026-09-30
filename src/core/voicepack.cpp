#include "voicedata.h"

#include <algorithm>
#include <cctype>

namespace y8 {
namespace {

// The MSX-AUDIO layout (the ROM's BV_* and BVO_*).
constexpr int kBasicTranFrac = 8;
constexpr int kBasicTranWhole = 9;
constexpr int kBasicFb = 10;
constexpr int kBasicMod = 16;
constexpr int kBasicCar = 24;
constexpr int kBasicMult = 0;
constexpr int kBasicTl = 1;
constexpr int kBasicAr = 2;
constexpr int kBasicSl = 3;
constexpr int kBasicWave = 5;

// Chunk 01 (VP_* and VO_*).
constexpr int kPackedFb = 0;
constexpr int kPackedTran = 1;
constexpr int kPackedMod = 2;
constexpr int kPackedCar = 7;
constexpr int kPackedTl = 0;
constexpr int kPackedAr = 1;
constexpr int kPackedSl = 2;
constexpr int kPackedMult = 3;
constexpr int kPackedWave = 4;

// Chunk 40, bytecode.md "OPM と OPN 系の音色". Operators M1 C1 M2 C2.
constexpr int kOpmAlgo = 0;   // bit7 NE, bit6 FX, bit5-3 FB, bit2-0 AL
constexpr int kOpmSens = 1;   // bit5-4 AMS, bit2-0 PMS
constexpr int kOpmTran = 2;
constexpr int kOpmKeys = 3;   // bit7-4 OP4-OP1
constexpr int kOpmOps = 4;
constexpr int kOpmOpSize = 7;
constexpr int kOpmTl = 0;
constexpr int kOpmKsAr = 1;
constexpr int kOpmAmDr = 2;
constexpr int kOpmDt2Sr = 3;
constexpr int kOpmSlRr = 4;
constexpr int kOpmSsgEg = 5;
constexpr int kOpmDtMl = 6;
constexpr int kOperators = 4;

// SFG BASIC's 40 byte body. Its operators are OP1-OP4 in the order of the
// algorithm diagrams, which is M1 C1 M2 C2, as chunk 40 has them.
constexpr int kSfgKeys = 3;   // bit6-3 OP4-OP1
constexpr int kSfgAlgo = 4;   // bit7-6 the outputs, bit5-3 FB, bit2-0 ALG
constexpr int kSfgSens = 5;   // bit6-4 PMS, bit1-0 AMS
constexpr int kSfgNoise = 6;  // bit7 the noise on
constexpr int kSfgTran = 7;
constexpr int kSfgOps = 8;
constexpr int kSfgOpSize = 8;
constexpr int kSfgTl = 0;
constexpr int kSfgDtMul = 3;
constexpr int kSfgKsAr = 4;
constexpr int kSfgAmD1r = 5;
constexpr int kSfgDt2D2r = 6;
constexpr int kSfgD1lRr = 7;

// Makoto BASIC's array of 50, N88-BASIC(86)'s with SSG-EG and AMS: row 0 the
// channel and rows 1-4 OP1-OP4 (M1 C1 M2 C2), ten columns each. Its rates and
// levels run the other way from the chip's registers.
constexpr int kMakotoColumns = 10;
constexpr int kMakotoAlgo = 0;  // row 0: bit5-3 FB, bit2-0 AL
constexpr int kMakotoKeys = 1;  // row 0: bit3-0 OP4-OP1
constexpr int kMakotoAms = 9;   // row 0
constexpr int kMakotoAr = 0;
constexpr int kMakotoDr = 1;
constexpr int kMakotoSr = 2;
constexpr int kMakotoRr = 3;
constexpr int kMakotoSl = 4;
constexpr int kMakotoTl = 5;
constexpr int kMakotoSsgKs = 6;  // SSG-EG * 16 + KS
constexpr int kMakotoMl = 7;
constexpr int kMakotoDt = 8;     // -3 to +3
constexpr int kMakotoAm = 9;

void packOperator(const VoiceRecord& b, int from, PackedVoice& p, int to) {
    p[to + kPackedTl] = b[from + kBasicTl];
    p[to + kPackedAr] = b[from + kBasicAr];
    p[to + kPackedSl] = b[from + kBasicSl];
    p[to + kPackedMult] = b[from + kBasicMult];
    p[to + kPackedWave] = b[from + kBasicWave] & 0x03;  // the bits BASIC's byte has
}

VoiceRecord packSfg(const std::vector<std::uint8_t>& s) {
    VoiceRecord r{};
    // The outputs are the pan, which 87 sets, and LFO speed, AMD, PMD and the
    // LFO waveform are the chip's alone: none of them is the voice's in chunk 40.
    r[kOpmAlgo] = static_cast<std::uint8_t>((s[kSfgNoise] & 0x80) | (s[kSfgAlgo] & 0x3F));
    r[kOpmSens] = static_cast<std::uint8_t>(((s[kSfgSens] & 0x03) << 4) | ((s[kSfgSens] >> 4) & 0x07));
    r[kOpmTran] = s[kSfgTran];
    r[kOpmKeys] = static_cast<std::uint8_t>((s[kSfgKeys] & 0x78) << 1);
    for (int op = 0; op < kOperators; ++op) {
        const std::uint8_t* t = &s[static_cast<std::size_t>(kSfgOps + op * kSfgOpSize)];
        std::uint8_t* o = &r[static_cast<std::size_t>(kOpmOps + op * kOpmOpSize)];
        // Velocity, level key scale and the TL offset are FB-01's; the chip has none.
        o[kOpmTl] = t[kSfgTl] & 0x7F;
        o[kOpmKsAr] = t[kSfgKsAr] & 0xDF;
        o[kOpmAmDr] = t[kSfgAmD1r] & 0x9F;
        o[kOpmDt2Sr] = t[kSfgDt2D2r] & 0xDF;
        o[kOpmSlRr] = t[kSfgD1lRr];
        o[kOpmDtMl] = t[kSfgDtMul] & 0x7F;
    }
    return r;
}

// OPN's DT: bit2 the sign, bit1-0 how far.
std::uint8_t opnDetune(std::uint8_t v) {
    const int d = static_cast<std::int8_t>(v);
    return static_cast<std::uint8_t>(d < 0 ? 0x04 | ((-d) & 0x03) : d & 0x03);
}

VoiceRecord packMakoto(const std::vector<std::uint8_t>& m) {
    auto at = [&](int row, int column) { return m[static_cast<std::size_t>(row * kMakotoColumns + column)]; };
    VoiceRecord r{};
    // Makoto BASIC reads no PM sensitivity or LFO from the array, so neither
    // comes across.
    r[kOpmAlgo] = at(0, kMakotoAlgo) & 0x3F;
    r[kOpmSens] = static_cast<std::uint8_t>((at(0, kMakotoAms) & 0x03) << 4);
    r[kOpmKeys] = static_cast<std::uint8_t>((at(0, kMakotoKeys) & 0x0F) << 4);
    for (int op = 0; op < kOperators; ++op) {
        const int row = op + 1;
        std::uint8_t* o = &r[static_cast<std::size_t>(kOpmOps + op * kOpmOpSize)];
        const std::uint8_t ssgKs = at(row, kMakotoSsgKs);
        o[kOpmTl] = static_cast<std::uint8_t>(0x7F - (at(row, kMakotoTl) & 0x7F));
        o[kOpmKsAr] = static_cast<std::uint8_t>(((ssgKs & 0x03) << 6) | (0x1F - (at(row, kMakotoAr) & 0x1F)));
        o[kOpmAmDr] = static_cast<std::uint8_t>((at(row, kMakotoAm) ? 0x80 : 0) |
                                                (0x1F - (at(row, kMakotoDr) & 0x1F)));
        o[kOpmDt2Sr] = static_cast<std::uint8_t>(0x1F - (at(row, kMakotoSr) & 0x1F));
        o[kOpmSlRr] = static_cast<std::uint8_t>(((0x0F - (at(row, kMakotoSl) & 0x0F)) << 4) |
                                                (0x0F - (at(row, kMakotoRr) & 0x0F)));
        o[kOpmSsgEg] = static_cast<std::uint8_t>(ssgKs >> 4);
        o[kOpmDtMl] = static_cast<std::uint8_t>((opnDetune(at(row, kMakotoDt)) << 4) | (at(row, kMakotoMl) & 0x0F));
    }
    return r;
}

struct FormatEntry {
    VoiceFormat format;
    const char* symbol;
    int size;
    VoiceFormat packed;
};

// The packed sizes are the record lengths of bytecode.md's chunks 01, 40 and 42.
const FormatEntry kFormats[] = {
    {VoiceFormat::Opl, "OPL", kPackedVoiceSize, VoiceFormat::Opl},
    {VoiceFormat::Opm, "OPM", 32, VoiceFormat::Opm},
    {VoiceFormat::Opl3, "OPL3", 24, VoiceFormat::Opl3},
    {VoiceFormat::Audio, "AUDIO", kVoiceRecordSize, VoiceFormat::Opl},
    {VoiceFormat::Sfg, "SFG", 40, VoiceFormat::Opm},
    {VoiceFormat::Makoto, "MAKOTO", 50, VoiceFormat::Opm},
};

const FormatEntry& entryOf(VoiceFormat f) {
    for (const FormatEntry& e : kFormats) {
        if (e.format == f) return e;
    }
    return kFormats[0];
}

} // namespace

PackedVoice packVoice(const VoiceRecord& b) {
    PackedVoice p{};
    p[kPackedFb] = b[kBasicFb] & 0x0F;
    p[kPackedTran] = static_cast<std::uint8_t>(b[kBasicTranWhole] + (b[kBasicTranFrac] >= 0x80 ? 1 : 0));
    packOperator(b, kBasicMod, p, kPackedMod);
    packOperator(b, kBasicCar, p, kPackedCar);
    return p;
}

int voiceFormatSize(VoiceFormat f) { return entryOf(f).size; }

const char* voiceFormatSymbol(VoiceFormat f) { return entryOf(f).symbol; }

VoiceFormat packedFormat(VoiceFormat f) { return entryOf(f).packed; }

bool parseVoiceFormat(const std::string& text, VoiceFormat& out) {
    std::string upper;
    for (char c : text) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    for (const FormatEntry& e : kFormats) {
        if (upper == e.symbol) {
            out = e.format;
            return true;
        }
    }
    return false;
}

std::vector<std::string> voiceFormatNames() {
    std::vector<std::string> names;
    for (const FormatEntry& e : kFormats) names.emplace_back(e.symbol);
    return names;
}

VoiceRecord packRecord(VoiceFormat f, const std::vector<std::uint8_t>& in) {
    switch (f) {
        case VoiceFormat::Audio: {
            VoiceRecord audio{};
            std::copy(in.begin(), in.end(), audio.begin());
            PackedVoice p = packVoice(audio);
            VoiceRecord r{};
            std::copy(p.begin(), p.end(), r.begin());
            return r;
        }
        case VoiceFormat::Sfg: return packSfg(in);
        case VoiceFormat::Makoto: return packMakoto(in);
        default: {
            VoiceRecord r{};
            std::copy(in.begin(), in.end(), r.begin());
            return r;
        }
    }
}

} // namespace y8
