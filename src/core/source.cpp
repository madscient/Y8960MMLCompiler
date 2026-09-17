#include "source.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace y8 {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }

// Splits on whitespace. Keeps where each word started, for the diagnostics.
struct Word {
    std::string text;
    int column = 0;
};

std::vector<Word> split(const std::string& s, std::size_t from) {
    std::vector<Word> words;
    std::size_t i = from;
    while (i < s.size()) {
        while (i < s.size() && isSpace(s[i])) ++i;
        if (i >= s.size()) break;
        std::size_t start = i;
        while (i < s.size() && !isSpace(s[i])) ++i;
        words.push_back({s.substr(start, i - start), static_cast<int>(start) + 1});
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

void doAssign(SourceFile& src, const std::string& line, int lineNo, Diagnostics& diag) {
    std::vector<Word> w = split(line, 1);
    if (w.size() != 4) {
        diag.error(src.path, lineNo, 1, "#assign takes a track name, a device and a channel");
        return;
    }
    const std::string& name = w[1].text;
    if (name.size() != 1 || name[0] < 'A' || name[0] > 'P') {
        diag.error(src.path, lineNo, w[1].column, "'" + name + "' is not a track name (A-P)");
        return;
    }
    int index = name[0] - 'A';

    Device dev;
    if (!parseDevice(w[2].text, dev)) {
        diag.error(src.path, lineNo, w[2].column, "'" + w[2].text + "' is not a device");
        return;
    }
    long channel = 0;
    if (!parseInt(w[3].text, channel) || !deviceHasChannel(dev, static_cast<int>(channel))) {
        diag.error(src.path, lineNo, w[3].column,
                   std::string(deviceSymbol(dev)) + " has no channel " + w[3].text);
        return;
    }

    TrackSource& t = src.tracks[index];
    if (t.assigned) {
        diag.error(src.path, lineNo, w[1].column,
                   "track " + name + " is already assigned (line " + std::to_string(t.assignLine) + ")");
        return;
    }
    for (int i = 0; i < kTrackCount; ++i) {
        const TrackSource& other = src.tracks[i];
        if (!other.assigned || other.device != dev) continue;
        if (other.channel == channel) {
            diag.error(src.path, lineNo, w[2].column,
                       std::string(deviceSymbol(dev)) + " channel " + w[3].text +
                           " is already track " + std::string(1, static_cast<char>('A' + i)));
            return;
        }
        // One block cannot run rhythm mode and channels 6-8 at the same time.
        bool otherRhythm = other.channel == kRhythmChannel;
        bool thisRhythm = channel == kRhythmChannel;
        bool otherUses = other.channel >= kRhythmUsesFirst && other.channel <= kRhythmUsesLast;
        bool thisUses = channel >= kRhythmUsesFirst && channel <= kRhythmUsesLast;
        if ((otherRhythm && thisUses) || (thisRhythm && otherUses)) {
            diag.error(src.path, lineNo, w[2].column,
                       std::string(deviceSymbol(dev)) + " cannot use channel " + w[3].text +
                           " and channel " + std::to_string(other.channel) +
                           " at once: rhythm mode takes channels 6-8");
            return;
        }
    }

    t.assigned = true;
    t.device = dev;
    t.channel = static_cast<int>(channel);
    t.assignLine = lineNo;
}

void doDefine(SourceFile& src, const std::string& line, int lineNo, Diagnostics& diag) {
    std::vector<Word> w = split(line, 1);
    if (w.size() < 3) {
        diag.error(src.path, lineNo, 1, "#define takes a name and a value");
        return;
    }
    const std::string& name = w[1].text;
    if (!isMacroName(name)) {
        diag.error(src.path, lineNo, w[1].column,
                   "'" + name + "' is not a name (a letter, then letters, digits and _)");
        return;
    }
    auto it = src.macros.find(name);
    if (it != src.macros.end()) {
        diag.error(src.path, lineNo, w[1].column,
                   "'" + name + "' is already defined (line " + std::to_string(it->second.line) + ")");
        return;
    }

    Macro m;
    m.line = lineNo;
    std::string quoted;
    std::size_t afterName = static_cast<std::size_t>(w[1].column - 1) + name.size();
    if (quotedRest(line, afterName, quoted)) {
        m.isString = true;
        m.text = quoted;
    } else if (w.size() == 3 && parseInt(w[2].text, m.number)) {
        m.isString = false;
    } else {
        diag.error(src.path, lineNo, w[2].column,
                   "a #define value is a number or a \"...\" string");
        return;
    }
    src.macros.emplace(name, std::move(m));
}

void doPcm(SourceFile& src, const std::string& line, int lineNo, Diagnostics& diag) {
    std::vector<Word> w = split(line, 1);
    if (w.size() != 2) {
        diag.error(src.path, lineNo, 1, "#pcm takes the path of one adpcm_packer JSON file");
        return;
    }
    if (!src.pcmJson.empty()) {
        diag.error(src.path, lineNo, 1,
                   "#pcm is already given (line " + std::to_string(src.pcmLine) + ")");
        return;
    }
    src.pcmJson = w[1].text;
    src.pcmLine = lineNo;
}

void doVoice(SourceFile& src, const std::string& line, int lineNo, Diagnostics& diag) {
    std::vector<Word> w = split(line, 1);
    if (w.size() != 3) {
        diag.error(src.path, lineNo, 1, "#voice takes a voice file number and an entry name");
        return;
    }
    long number = 0;
    if (!parseInt(w[1].text, number) || number < 0 || number > 31) {
        diag.error(src.path, lineNo, w[1].column, "a voice file number is 0 to 31");
        return;
    }
    for (const VoiceBinding& v : src.voices) {
        if (v.number == number) {
            diag.error(src.path, lineNo, w[1].column,
                       "voice file " + w[1].text + " is already bound (line " +
                           std::to_string(v.line) + ")");
            return;
        }
    }
    src.voices.push_back({static_cast<int>(number), w[2].text, lineNo});
}

void doTrackLine(SourceFile& src, const std::string& line, int lineNo) {
    int index = line[0] - 'A';
    TrackSource& t = src.tracks[index];
    std::size_t i = 1;
    while (i < line.size() && isSpace(line[i])) ++i;
    if (!t.text.empty()) t.text.push_back('\n');
    t.marks.push_back({t.text.size(), lineNo, static_cast<int>(i) + 1});
    t.text.append(line, i, std::string::npos);
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

    int lineNo = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, (nl == std::string::npos ? text.size() : nl) - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        bool blank = true;
        for (char c : line) {
            if (!isSpace(c)) {
                blank = false;
                break;
            }
        }
        if (blank) continue;

        char head = line[0];
        if (head == ';') continue;
        if (head == '#') {
            std::vector<Word> w = split(line, 1);
            std::string name = w.empty() ? std::string() : lower(w[0].text);
            if (name == "assign") {
                doAssign(out, line, lineNo, diag);
            } else if (name == "define") {
                doDefine(out, line, lineNo, diag);
            } else if (name == "pcm") {
                doPcm(out, line, lineNo, diag);
            } else if (name == "voice") {
                doVoice(out, line, lineNo, diag);
            } else {
                diag.error(path, lineNo, 1, "'#" + name + "' is not a meta command");
            }
            continue;
        }
        if (head >= 'A' && head <= 'P') {
            if (line.size() > 1 && !isSpace(line[1])) {
                diag.error(path, lineNo, 2, "a track name is followed by a space");
                continue;
            }
            doTrackLine(out, line, lineNo);
            continue;
        }
        diag.error(path, lineNo, 1,
                   "a line begins with a track name (A-P), '#' or ';'");
    }

    // #voice needs a #pcm to name entries in.
    if (out.pcmJson.empty() && !out.voices.empty()) {
        diag.error(path, out.voices.front().line, 1, "#voice needs a #pcm before it");
    } else {
        for (const VoiceBinding& v : out.voices) {
            if (v.line < out.pcmLine) {
                diag.error(path, v.line, 1, "#voice comes after the #pcm it names entries in");
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
