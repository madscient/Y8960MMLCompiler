#include "source.h"

#include "adpcm.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace y8 {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }

// One line as the rest of the file sees it: the physical lines a trailing '\'
// joined, and where each of them began.
struct Logical {
    std::string text;
    std::vector<OriginMark> segments;

    void locate(std::size_t offset, int& line, int& column) const {
        line = 0;
        column = 0;
        for (const OriginMark& s : segments) {
            if (s.offset > offset) break;
            line = s.line;
            column = s.column + static_cast<int>(offset - s.offset);
        }
    }
};

// Splits on whitespace. Keeps where each word started, for the diagnostics.
struct Word {
    std::string text;
    std::size_t offset = 0;
};

std::vector<Word> split(const std::string& s, std::size_t from) {
    std::vector<Word> words;
    std::size_t i = from;
    while (i < s.size()) {
        while (i < s.size() && isSpace(s[i])) ++i;
        if (i >= s.size()) break;
        std::size_t start = i;
        while (i < s.size() && !isSpace(s[i])) ++i;
        words.push_back({s.substr(start, i - start), start});
    }
    return words;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool parseInt(const std::string& s, long& out) {
    if (s.empty()) return false;
    std::size_t i = 0;
    bool neg = false;
    if (s[0] == '+' || s[0] == '-') {
        neg = (s[0] == '-');
        i = 1;
    }
    if (i >= s.size()) return false;
    long v = 0;
    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        v = v * 10 + (s[i] - '0');
        if (v > 1000000) return false;
    }
    out = neg ? -v : v;
    return true;
}

bool isMacroName(const std::string& s) {
    if (s.empty() || !std::isalpha(static_cast<unsigned char>(s[0]))) return false;
    for (char c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    return true;
}

// #define NAME "..." - the rest of the line after the name, with the quotes
// taken off. Returns false when the quotes are not there.
bool quotedRest(const std::string& line, std::size_t from, std::string& out) {
    std::size_t i = from;
    while (i < line.size() && isSpace(line[i])) ++i;
    if (i >= line.size() || line[i] != '"') return false;
    std::size_t close = line.rfind('"');
    if (close <= i) return false;
    out = line.substr(i + 1, close - i - 1);
    for (std::size_t j = close + 1; j < line.size(); ++j) {
        if (!isSpace(line[j])) return false;
    }
    return true;
}

// Everything the meta command handlers need to report where they are.
struct Context {
    SourceFile& src;
    const Logical& line;
    Diagnostics& diag;

    void error(std::size_t offset, const std::string& message) const {
        int l = 0, c = 0;
        line.locate(offset, l, c);
        diag.error(src.path, l, c, message);
    }
};

void doAssign(const Context& ctx) {
    const std::string& line = ctx.line.text;
    std::vector<Word> w = split(line, 1);
    if (w.size() != 4) {
        ctx.error(0, "#assign takes a track name, a device and a channel");
        return;
    }
    const std::string& name = w[1].text;
    if (name.size() != 1 || name[0] < 'A' || name[0] > 'P') {
        ctx.error(w[1].offset, "'" + name + "' is not a track name (A-P)");
        return;
    }
    int index = name[0] - 'A';

    Device dev;
    if (!parseDevice(w[2].text, dev)) {
        ctx.error(w[2].offset, "'" + w[2].text + "' is not a device");
        return;
    }
    long channel = 0;
    if (!parseInt(w[3].text, channel) || !deviceHasChannel(dev, static_cast<int>(channel))) {
        ctx.error(w[3].offset, std::string(deviceSymbol(dev)) + " has no channel " + w[3].text);
        return;
    }

    TrackSource& t = ctx.src.tracks[index];
    if (t.assigned) {
        ctx.error(w[1].offset, "track " + name + " is already assigned (line " +
                                   std::to_string(t.assignLine) + ")");
        return;
    }
    for (int i = 0; i < kTrackCount; ++i) {
        const TrackSource& other = ctx.src.tracks[i];
        if (!other.assigned || other.device != dev) continue;
        if (other.channel == channel) {
            ctx.error(w[2].offset, std::string(deviceSymbol(dev)) + " channel " + w[3].text +
                                       " is already track " +
                                       std::string(1, static_cast<char>('A' + i)));
            return;
        }
        // One block cannot run rhythm mode and channels 6-8 at the same time.
        bool otherRhythm = other.channel == kRhythmChannel;
        bool thisRhythm = channel == kRhythmChannel;
        bool otherUses = other.channel >= kRhythmUsesFirst && other.channel <= kRhythmUsesLast;
        bool thisUses = channel >= kRhythmUsesFirst && channel <= kRhythmUsesLast;
        if ((otherRhythm && thisUses) || (thisRhythm && otherUses)) {
            ctx.error(w[2].offset, std::string(deviceSymbol(dev)) + " cannot use channel " +
                                       w[3].text + " and channel " +
                                       std::to_string(other.channel) +
                                       " at once: rhythm mode takes channels 6-8");
            return;
        }
    }

    t.assigned = true;
    t.device = dev;
    t.channel = static_cast<int>(channel);
    t.assignLine = ctx.line.segments.front().line;
}

void doDefine(const Context& ctx) {
    const std::string& line = ctx.line.text;
    std::vector<Word> w = split(line, 1);
    if (w.size() < 3) {
        ctx.error(0, "#define takes a name and a value");
        return;
    }
    const std::string& name = w[1].text;
    if (!isMacroName(name)) {
        ctx.error(w[1].offset,
                  "'" + name + "' is not a name (a letter, then letters, digits and _)");
        return;
    }
    auto it = ctx.src.macros.find(name);
    if (it != ctx.src.macros.end()) {
        ctx.error(w[1].offset,
                  "'" + name + "' is already defined (line " + std::to_string(it->second.line) + ")");
        return;
    }

    Macro m;
    m.line = ctx.line.segments.front().line;
    std::string quoted;
    if (quotedRest(line, w[1].offset + name.size(), quoted)) {
        m.isString = true;
        m.text = quoted;
    } else if (w.size() == 3 && parseInt(w[2].text, m.number)) {
        m.isString = false;
    } else {
        ctx.error(w[2].offset, "a #define value is a number or a \"...\" string");
        return;
    }
    ctx.src.macros.emplace(name, std::move(m));
}

void doPcmBank(const Context& ctx) {
    std::vector<Word> w = split(ctx.line.text, 1);
    if (w.size() != 2) {
        ctx.error(0, "#pcmbank takes the path of one adpcm_packer JSON file or one Y8PC file");
        return;
    }
    if (!ctx.src.pcmBankPath.empty()) {
        ctx.error(0,
                  "#pcmbank is already given (line " + std::to_string(ctx.src.pcmBankLine) + ")");
        return;
    }
    ctx.src.pcmBankPath = w[1].text;
    ctx.src.pcmBankLine = ctx.line.segments.front().line;
}

void doAdpcm(const Context& ctx) {
    std::vector<Word> w = split(ctx.line.text, 1);
    if (w.size() != 3) {
        ctx.error(0, "#adpcm takes a voice file number and an entry name");
        return;
    }
    long number = 0;
    if (!parseInt(w[1].text, number) || number < 0 || number >= kPcmVoiceMax) {
        ctx.error(w[1].offset,
                  "a voice file number is 0 to " + std::to_string(kPcmVoiceMax - 1));
        return;
    }
    for (const SampleBinding& s : ctx.src.samples) {
        if (s.number == number) {
            ctx.error(w[1].offset, "voice file " + w[1].text + " is already bound (line " +
                                       std::to_string(s.line) + ")");
            return;
        }
    }
    ctx.src.samples.push_back(
        {static_cast<int>(number), w[2].text, ctx.line.segments.front().line});
}

// A comma separated list of `size` bytes. An item is a number: $hh is the byte
// as it stands. A record also takes a "..." string, which contributes its
// characters, and a negative decimal, which is the form a waveform level is
// written in; an envelope's four values are rates and a level, and take
// neither.
bool byteList(const Context& ctx, std::size_t from, std::size_t size, bool record,
              std::vector<std::uint8_t>& bytes) {
    const std::string& line = ctx.line.text;
    const std::string what = record ? "a record is " + std::to_string(size) + " bytes"
                                    : "#env takes " + std::to_string(size) + " values";
    std::size_t i = from;

    for (;;) {
        while (i < line.size() && isSpace(line[i])) ++i;
        if (i >= line.size()) {
            ctx.error(i, "a value was expected after the ','");
            return false;
        }
        const std::size_t itemAt = i;

        if (record && line[i] == '"') {
            std::size_t close = line.find('"', i + 1);
            if (close == std::string::npos) {
                ctx.error(i, "this string has no closing quote");
                return false;
            }
            for (std::size_t j = i + 1; j < close; ++j) {
                bytes.push_back(static_cast<std::uint8_t>(line[j]));
            }
            i = close + 1;
        } else {
            std::size_t start = i;
            while (i < line.size() && !isSpace(line[i]) && line[i] != ',') ++i;
            const std::string item = line.substr(start, i - start);
            long v = 0;
            if (item.size() > 1 && item[0] == '$') {
                v = 0;
                bool good = item.size() >= 2;
                for (std::size_t j = 1; j < item.size(); ++j) {
                    char c = static_cast<char>(std::tolower(static_cast<unsigned char>(item[j])));
                    int d;
                    if (c >= '0' && c <= '9') d = c - '0';
                    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                    else { good = false; break; }
                    v = v * 16 + d;
                }
                if (!good || v > 255) {
                    ctx.error(start, "'" + item + "' is not a byte written as $00 to $FF");
                    return false;
                }
            } else if (!parseInt(item, v) || v < (record ? -128 : 0) || v > 255) {
                ctx.error(start, "'" + item + "' is not a value; a byte is " +
                                     (record ? "-128" : "0") + " to 255");
                return false;
            }
            bytes.push_back(static_cast<std::uint8_t>(v & 0xFF));
        }

        if (bytes.size() > size) {
            ctx.error(itemAt, what + "; this one runs past the end");
            return false;
        }

        while (i < line.size() && isSpace(line[i])) ++i;
        if (i >= line.size()) break;
        if (line[i] != ',') {
            ctx.error(i, "',' was expected between values");
            return false;
        }
        ++i;
    }

    if (bytes.size() != size) {
        ctx.error(from, what + "; this is " + std::to_string(bytes.size()));
        return false;
    }
    return true;
}

void doRecord(const Context& ctx, bool wave) {
    const std::string& line = ctx.line.text;
    const char* what = wave ? "#wave" : "#voice";
    const int first = wave ? kUserWaveFirst : kUserVoiceFirst;
    const int last = wave ? kUserWaveLast : kUserVoiceLast;

    std::vector<Word> w = split(line, 1);
    // "#voice OPL @128 ...": the format first, then the number as MML names it.
    // A waveform has one layout, and "#wave 16 ..." takes neither.
    VoiceFormat format = VoiceFormat::Opl;
    std::size_t numberWord = 1;
    std::size_t size = kVoiceRecordSize;
    if (!wave) {
        std::string names;
        for (const std::string& n : voiceFormatNames()) names += (names.empty() ? "" : ", ") + n;
        if (w.size() < 2 || !parseVoiceFormat(w[1].text, format)) {
            ctx.error(w.size() < 2 ? 0 : w[1].offset, "#voice takes a format first: " + names);
            return;
        }
        size = static_cast<std::size_t>(voiceFormatSize(format));
        if (w.size() < 3) {
            ctx.error(w[1].offset, "#voice " + w[1].text + " takes @n and " + std::to_string(size) +
                                       " values");
            return;
        }
        if (w[2].text[0] != '@') {
            ctx.error(w[2].offset, "#voice takes the voice number as @n, as MML names it");
            return;
        }
        numberWord = 2;
    } else if (w.size() < 2) {
        ctx.error(0, "#wave takes a number and " + std::to_string(kVoiceRecordSize) + " bytes");
        return;
    }
    const Word& numberAt = w[numberWord];
    const std::string numberText = wave ? numberAt.text : numberAt.text.substr(1);
    long number = 0;
    if (!parseInt(numberText, number) || number < first || number > last) {
        ctx.error(numberAt.offset, std::string(what) + " takes a number from " +
                                       std::to_string(first) + " to " + std::to_string(last) +
                                       "; the ones below that are presets");
        return;
    }
    if (w.size() < numberWord + 2) {
        ctx.error(numberAt.offset, std::string(what) + " takes " + std::to_string(size) +
                                       " values after the number");
        return;
    }

    std::map<int, RecordDef>& into = wave ? ctx.src.userWaves : ctx.src.userVoices;
    auto it = into.find(static_cast<int>(number));
    if (it != into.end()) {
        ctx.error(numberAt.offset, std::string(what) + " " + numberText + " is already defined (line " +
                                       std::to_string(it->second.line) + ")");
        return;
    }

    RecordDef def;
    def.line = ctx.line.segments.front().line;
    std::vector<std::uint8_t> bytes;
    if (!byteList(ctx, w[numberWord + 1].offset, size, true, bytes)) return;
    if (wave) {
        std::copy(bytes.begin(), bytes.end(), def.record.begin());
    } else {
        def.format = packedFormat(format);
        def.record = packRecord(format, bytes);
    }
    into.emplace(static_cast<int>(number), def);
}

// "#env MUSICA @E1 16,20,8,10": the format first, then the number as MML names
// it. A MUSICA rate past 32 or a level past 15 is refused rather than rounded
// the way ENV COPY does, so a slip shows.
void doEnv(const Context& ctx) {
    const std::string& line = ctx.line.text;
    std::vector<Word> w = split(line, 1);
    EnvFormat format = EnvFormat::Musica;
    if (w.size() < 2 || !parseEnvFormat(w[1].text, format)) {
        std::string names;
        for (const std::string& n : envFormatNames()) names += (names.empty() ? "" : ", ") + n;
        ctx.error(w.size() < 2 ? 0 : w[1].offset, "#env takes a format first: " + names);
        return;
    }
    const std::string takes = " takes @En and " + std::to_string(kEnvValues) + " values: AR, DR, SL, RR";
    if (w.size() < 3) {
        ctx.error(w[1].offset, "#env " + w[1].text + takes);
        return;
    }
    const Word& numberAt = w[2];
    long number = 0;
    if (numberAt.text.size() < 3 || numberAt.text[0] != '@' ||
        std::tolower(static_cast<unsigned char>(numberAt.text[1])) != 'e') {
        ctx.error(numberAt.offset, "#env takes the envelope number as @En, as MML names it");
        return;
    }
    if (!parseInt(numberAt.text.substr(2), number) || number < 1 || number > kEnvMax) {
        ctx.error(numberAt.offset, "#env takes @E1 to @E" + std::to_string(kEnvMax) +
                                       "; @E0 is no envelope");
        return;
    }
    if (w.size() < 4) {
        ctx.error(numberAt.offset, "#env " + w[1].text + takes);
        return;
    }
    auto it = ctx.src.envelopes.find(static_cast<int>(number));
    if (it != ctx.src.envelopes.end()) {
        ctx.error(numberAt.offset, "#env " + numberAt.text + " is already defined (line " +
                                       std::to_string(it->second.line) + ")");
        return;
    }

    EnvDef def;
    def.line = ctx.line.segments.front().line;
    std::vector<std::uint8_t> bytes;
    if (!byteList(ctx, w[3].offset, def.values.size(), false, bytes)) return;

    static const char* const kNames[kEnvValues] = {"AR", "DR", "SL", "RR"};
    constexpr int kLevel = 2;
    for (int i = 0; i < kEnvValues; ++i) {
        const std::uint8_t v = bytes[static_cast<std::size_t>(i)];
        if (i == kLevel) {
            if (v > kEnvLevelMax) {
                ctx.error(w[3].offset, "SL is 0 to " + std::to_string(kEnvLevelMax));
                return;
            }
            def.values[static_cast<std::size_t>(i)] = v;
        } else if (format == EnvFormat::Musica) {
            if (!envRateByte(v, def.values[static_cast<std::size_t>(i)])) {
                ctx.error(w[3].offset, std::string(kNames[i]) + " is 0 to " + std::to_string(kEnvRateMax));
                return;
            }
        } else {
            if (!envRateValid(v)) {
                ctx.error(w[3].offset, std::string(kNames[i]) +
                                           " is frames in the high four bits and a step in the low "
                                           "four, both 1 to 15");
                return;
            }
            def.values[static_cast<std::size_t>(i)] = v;
        }
    }
    ctx.src.envelopes.emplace(static_cast<int>(number), def);
}

// The one list of meta commands. syntaxes/y8960mml.tmLanguage.json names them
// too, and tests/grammar_test.cpp holds the two lists to each other.
// #title "..." and #author "...". The quotes are the first and the last on the
// line, so a '"' between them is part of the string.
void doMetaText(const Context& ctx, const char* what, MetaText MetaInfo::*field) {
    const std::string& line = ctx.line.text;
    MetaText& t = ctx.src.meta.*field;
    if (t.line != 0) {
        ctx.error(0, std::string(what) + " is already given (line " + std::to_string(t.line) + ")");
        return;
    }
    std::vector<Word> w = split(line, 1);
    const std::size_t from = w[0].offset + w[0].text.size();
    std::string text;
    if (!quotedRest(line, from, text)) {
        ctx.error(from, std::string(what) + " takes a \"...\" string");
        return;
    }
    const std::size_t open = line.find('"', from);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < kMetaCharMin || c > kMetaCharMax) {
            ctx.error(open + 1 + i, std::string(what) +
                                        " is written in ASCII alone, letters, digits, space and signs");
            return;
        }
    }
    if (text.size() > static_cast<std::size_t>(kMetaTextMax)) {
        ctx.error(open, std::string(what) + " is at most " + std::to_string(kMetaTextMax) +
                            " characters; this is " + std::to_string(text.size()));
        return;
    }
    t.text = text;
    t.line = ctx.line.segments.front().line;
}

// #pitch 442.5: the frequency of A4, to a tenth of a hertz.
void doPitch(const Context& ctx) {
    const std::string& line = ctx.line.text;
    MetaInfo& meta = ctx.src.meta;
    if (meta.pitchLine != 0) {
        ctx.error(0, "#pitch is already given (line " + std::to_string(meta.pitchLine) + ")");
        return;
    }
    std::vector<Word> w = split(line, 1);
    if (w.size() != 2) {
        ctx.error(0, "#pitch takes one frequency in Hz, as 440.0");
        return;
    }
    const std::string& s = w[1].text;
    const std::size_t dot = s.find('.');
    const std::string whole = s.substr(0, dot);
    const std::string tenth = dot == std::string::npos ? "0" : s.substr(dot + 1);
    auto digits = [](const std::string& d) {
        return !d.empty() && d.find_first_not_of("0123456789") == std::string::npos;
    };
    if (!digits(whole) || whole.size() > 3 || !digits(tenth) || tenth.size() != 1) {
        ctx.error(w[1].offset, "#pitch takes Hz to one decimal place, as 440.0 or 442");
        return;
    }
    const int pitch = std::stoi(whole) * 10 + (tenth[0] - '0');
    if (pitch < kPitchMin || pitch > kPitchMax) {
        ctx.error(w[1].offset, "#pitch is 430.0 to 450.0");
        return;
    }
    meta.pitch = pitch;
    meta.pitchLine = ctx.line.segments.front().line;
}

struct MetaCommand {
    const char* name;
    void (*handler)(const Context&);
};

const MetaCommand kMetaCommands[] = {
    {"assign", doAssign},
    {"define", doDefine},
    {"voice", [](const Context& c) { doRecord(c, false); }},
    {"wave", [](const Context& c) { doRecord(c, true); }},
    {"env", doEnv},
    {"pcmbank", doPcmBank},
    {"adpcm", doAdpcm},
    {"title", [](const Context& c) { doMetaText(c, "#title", &MetaInfo::title); }},
    {"author", [](const Context& c) { doMetaText(c, "#author", &MetaInfo::author); }},
    {"pitch", doPitch},
};

void doTrackLine(SourceFile& src, const Logical& line) {
    int index = line.text[0] - 'A';
    TrackSource& t = src.tracks[index];
    std::size_t i = 1;
    while (i < line.text.size() && isSpace(line.text[i])) ++i;

    if (!t.text.empty()) t.text.push_back('\n');
    const std::size_t base = t.text.size();

    int l = 0, c = 0;
    line.locate(i, l, c);
    t.marks.push_back({base, l, c});
    // A line continued with '\' keeps a mark per physical line, so a
    // diagnostic in the continued part still names the line it is on.
    for (const OriginMark& s : line.segments) {
        if (s.offset > i) t.marks.push_back({base + (s.offset - i), s.line, s.column});
    }
    t.text.append(line.text, i, std::string::npos);
}

} // namespace

std::vector<std::string> metaCommandNames() {
    std::vector<std::string> names;
    for (const MetaCommand& m : kMetaCommands) names.emplace_back(m.name);
    return names;
}

namespace {

// The ROM's ENVRTAB (src/tab/envdat.asm): rate n of 0-32 as frames << 4 | step.
const std::uint8_t kEnvRates[kEnvRateMax + 1] = {
    0xF1, 0xC1, 0xA1, 0x91, 0x81, 0x71, 0x61, 0x51,
    0x41, 0x72, 0x31, 0x52, 0x21, 0x53, 0x32, 0x43,
    0x11, 0x34, 0x23, 0x35, 0x12, 0x25, 0x13, 0x27,
    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1C,
    0x1F,
};

const struct {
    EnvFormat format;
    const char* symbol;
} kEnvFormats[] = {
    {EnvFormat::Musica, "MUSICA"},
    {EnvFormat::Raw, "RAW"},
};

} // namespace

const char* envFormatSymbol(EnvFormat f) {
    for (const auto& e : kEnvFormats) {
        if (e.format == f) return e.symbol;
    }
    return "";
}

bool parseEnvFormat(const std::string& text, EnvFormat& out) {
    std::string upper;
    for (char c : text) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    for (const auto& e : kEnvFormats) {
        if (upper == e.symbol) {
            out = e.format;
            return true;
        }
    }
    return false;
}

std::vector<std::string> envFormatNames() {
    std::vector<std::string> names;
    for (const auto& e : kEnvFormats) names.emplace_back(e.symbol);
    return names;
}

bool envRateByte(int rate, std::uint8_t& out) {
    if (rate < 0 || rate > kEnvRateMax) return false;
    out = kEnvRates[rate];
    return true;
}

bool envRateOf(std::uint8_t byte, int& out) {
    for (int i = 0; i <= kEnvRateMax; ++i) {
        if (kEnvRates[i] == byte) {
            out = i;
            return true;
        }
    }
    return false;
}

bool envRateValid(std::uint8_t byte) {
    return (byte >> 4) != 0 && (byte & 0x0F) != 0;
}

void TrackSource::locate(std::size_t offset, int& line, int& column) const {
    line = 0;
    column = 0;
    for (const OriginMark& m : marks) {
        if (m.offset > offset) break;
        line = m.line;
        column = m.column + static_cast<int>(offset - m.offset);
    }
}

bool readSourceText(const std::string& path, const std::string& text, SourceFile& out,
                    Diagnostics& diag) {
    out.path = path;

    std::size_t pos = 0;
    // A BOM is not part of the first line.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        pos = 3;
    }

    std::vector<std::string> physical;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, (nl == std::string::npos ? text.size() : nl) - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        physical.push_back(std::move(line));
    }

    for (std::size_t n = 0; n < physical.size();) {
        Logical logical;
        // A '\' at the end of a line drops the line break and joins the next.
        // It is read before anything else looks at the line, so it works on
        // every kind of line a comment one included.
        for (;;) {
            std::string part = physical[n];
            while (!part.empty() && isSpace(part.back())) part.pop_back();
            bool joins = !part.empty() && part.back() == '\\';
            if (joins) part.pop_back();

            logical.segments.push_back({logical.text.size(), static_cast<int>(n) + 1, 1});
            logical.text += part;
            ++n;
            if (!joins || n >= physical.size()) break;
        }

        const std::string& line = logical.text;
        bool blank = true;
        for (char c : line) {
            if (!isSpace(c)) {
                blank = false;
                break;
            }
        }
        if (blank) continue;

        const int firstLine = logical.segments.front().line;
        char head = line[0];
        if (head == ';') continue;
        if (head == '#') {
            Context ctx{out, logical, diag};
            std::vector<Word> w = split(line, 1);
            std::string name = w.empty() ? std::string() : lower(w[0].text);
            const MetaCommand* found = nullptr;
            for (const MetaCommand& m : kMetaCommands) {
                if (name == m.name) found = &m;
            }
            if (found) {
                found->handler(ctx);
            } else {
                diag.error(path, firstLine, 1, "'#" + name + "' is not a meta command");
            }
            continue;
        }
        if (head >= 'A' && head <= 'P') {
            if (line.size() > 1 && !isSpace(line[1])) {
                diag.error(path, firstLine, 2, "a track name is followed by a space");
                continue;
            }
            doTrackLine(out, logical);
            continue;
        }
        diag.error(path, firstLine, 1, "a line begins with a track name (A-P), '#' or ';'");
    }

    // #adpcm needs a #pcmbank to name entries in.
    if (out.pcmBankPath.empty() && !out.samples.empty()) {
        diag.error(path, out.samples.front().line, 1, "#adpcm needs a #pcmbank before it");
    } else {
        for (const SampleBinding& s : out.samples) {
            if (s.line < out.pcmBankLine) {
                diag.error(path, s.line, 1,
                           "#adpcm comes after the #pcmbank it names entries in");
            }
        }
    }

    return !diag.hasErrors();
}

bool readSource(const std::string& path, SourceFile& out, Diagnostics& diag) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        diag.error(path, 0, 0, "cannot open the file");
        return false;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return readSourceText(path, buf.str(), out, diag);
}

} // namespace y8
