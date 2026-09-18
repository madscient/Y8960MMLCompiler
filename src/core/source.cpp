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
        ctx.error(0, "#pcmbank takes the path of one adpcm_packer JSON file");
        return;
    }
    if (!ctx.src.pcmBankJson.empty()) {
        ctx.error(0,
                  "#pcmbank is already given (line " + std::to_string(ctx.src.pcmBankLine) + ")");
        return;
    }
    ctx.src.pcmBankJson = w[1].text;
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

// The bytes of a record, written as a comma separated list. An item is a
// "..." string, which contributes its characters, or a number: $hh is the byte
// as it stands and a decimal one may be negative, which is the form a waveform
// level is written in.
bool recordBytes(const Context& ctx, std::size_t from, VoiceRecord& out) {
    const std::string& line = ctx.line.text;
    std::vector<std::uint8_t> bytes;
    std::size_t i = from;

    for (;;) {
        while (i < line.size() && isSpace(line[i])) ++i;
        if (i >= line.size()) {
            ctx.error(i, "a value was expected after the ','");
            return false;
        }
        const std::size_t itemAt = i;

        if (line[i] == '"') {
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
            } else if (!parseInt(item, v) || v < -128 || v > 255) {
                ctx.error(start, "'" + item + "' is not a value; a byte is -128 to 255");
                return false;
            }
            bytes.push_back(static_cast<std::uint8_t>(v & 0xFF));
        }

        if (bytes.size() > static_cast<std::size_t>(kVoiceRecordSize)) {
            ctx.error(itemAt, "a record is " + std::to_string(kVoiceRecordSize) +
                                  " bytes; this one runs past the end");
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

    if (bytes.size() != static_cast<std::size_t>(kVoiceRecordSize)) {
        ctx.error(from, "a record is " + std::to_string(kVoiceRecordSize) + " bytes; this is " +
                            std::to_string(bytes.size()));
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), out.begin());
    return true;
}

void doRecord(const Context& ctx, bool wave) {
    const std::string& line = ctx.line.text;
    const char* what = wave ? "#wave" : "#voice";
    const int first = wave ? kUserWaveFirst : kUserVoiceFirst;
    const int last = wave ? kUserWaveLast : kUserVoiceLast;

    std::vector<Word> w = split(line, 1);
    if (w.size() < 3) {
        ctx.error(0, std::string(what) + " takes a number and " +
                         std::to_string(kVoiceRecordSize) + " bytes");
        return;
    }
    long number = 0;
    if (!parseInt(w[1].text, number) || number < first || number > last) {
        ctx.error(w[1].offset, std::string(what) + " takes a number from " +
                                   std::to_string(first) + " to " + std::to_string(last) +
                                   "; the ones below that are presets");
        return;
    }

    std::map<int, RecordDef>& into = wave ? ctx.src.userWaves : ctx.src.userVoices;
    auto it = into.find(static_cast<int>(number));
    if (it != into.end()) {
        ctx.error(w[1].offset, std::string(what) + " " + w[1].text + " is already defined (line " +
                                   std::to_string(it->second.line) + ")");
        return;
    }

    RecordDef def;
    def.line = ctx.line.segments.front().line;
    if (!recordBytes(ctx, w[2].offset, def.record)) return;
    into.emplace(static_cast<int>(number), def);
}

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
            if (name == "assign") {
                doAssign(ctx);
            } else if (name == "define") {
                doDefine(ctx);
            } else if (name == "pcmbank") {
                doPcmBank(ctx);
            } else if (name == "adpcm") {
                doAdpcm(ctx);
            } else if (name == "voice") {
                doRecord(ctx, false);
            } else if (name == "wave") {
                doRecord(ctx, true);
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
    if (out.pcmBankJson.empty() && !out.samples.empty()) {
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
