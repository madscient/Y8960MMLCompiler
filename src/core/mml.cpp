#include "mml.h"

#include <cctype>
#include <string>

#include "opcodes.h"

namespace y8 {
namespace {

constexpr int kOctaveMax = 8;
constexpr int kQuantMax = 8;
constexpr int kVolMax = 15;
constexpr int kMixerMax = 127;
constexpr int kTempoMin = 32;
constexpr int kRhyVolMax = 15;
constexpr int kVoiceUserFirst = 128;  // @128-@191, the voices VOICE COPY makes
constexpr int kVoiceUserLast = 191;
constexpr int kWaveUserFirst = 16;    // @16-@31 on the SCC, WAVE COPY's
constexpr int kWaveUserLast = 31;
constexpr int kOpllBanked = 64;       // the one number naming no voice

// a b c d e f g, as semitones from c.
const int kSemitone[7] = {9, 11, 0, 2, 4, 5, 7};

struct Fail {};  // thrown to unwind to the end of the track being compiled

class TrackCompiler {
public:
    TrackCompiler(const SourceFile& src, int index, VoiceSet& voices, Diagnostics& diag)
        : src_(src), track_(src.tracks[index]), voices_(voices), diag_(diag) {
        dialect_ = dialectFor(track_.device, track_.channel);
        family_ = deviceFamily(track_.device);
    }

    bool run(TrackCode& out, std::set<int>& adpcmVoiceFiles);

private:
    // ---- the reader -------------------------------------------------------
    struct View {
        const std::string* text;
        std::size_t pos;
        bool macro;
    };

    bool peek(char& c);
    void skip();
    bool next(char& c);

    // ---- numbers and lengths ---------------------------------------------
    bool getNum(long& out);          // false when no number is there
    long needNum();                  // errors out when no number is there
    long getSigned();
    long optNum(long def);           // a number if one is there, else `def`
    int getLen();                    // ticks, using the running default
    int dots(int base);

    // ---- output -----------------------------------------------------------
    void emit(std::uint8_t b) { bytes_.push_back(b); }
    void emit(std::uint8_t op, std::uint8_t a) { emit(op); emit(a); }
    void emit(std::uint8_t op, std::uint8_t a, std::uint8_t b) { emit(op, a); emit(b); }
    void emitWord(std::uint16_t v) {
        emit(static_cast<std::uint8_t>(v & 0xFF));
        emit(static_cast<std::uint8_t>(v >> 8));
    }
    void emitLen(int ticks);
    void patch(std::size_t field, std::size_t target);

    // ---- commands ---------------------------------------------------------
    void dispatch(char c);
    void melodyCommand(char c);
    void rhythmCommand(char c);

    void cmdNote(char c);
    void cmdNoteAbs();
    void cmdRest();
    void cmdOctave();
    void cmdOctUp();
    void cmdOctDown();
    void cmdLength();
    void cmdVolume();
    void cmdAt();
    void cmdTempo();
    void cmdQuantize();
    void cmdShape();
    void cmdPeriod();
    void cmdPan();
    void cmdBend(std::uint8_t op);
    void cmdPorta();
    void cmdReg();
    void cmdTupletStart();
    void cmdTupletEnd();
    void cmdXString();
    void cmdRhythmInstruments(char c);
    void cmdRhythmVolume();
    void cmdRhythmAt();

    void cmdLoopStart();
    void cmdLoopEnd();
    void cmdBlockStart();
    void cmdBlockEnd();
    void cmdParen();

    void voiceNumber(long n);
    void defaultVoice();
    void noteEmit(std::uint8_t op, int ticks);
    void timeFlag(int ticks);

    void blockAuto(char nextCommand);
    void blockEnd();
    void blockClose();

    int tupletCount();

    [[noreturn]] void fail(const std::string& message);
    [[noreturn]] void failAt(std::size_t offset, const std::string& message);

    const SourceFile& src_;
    const TrackSource& track_;
    VoiceSet& voices_;
    Diagnostics& diag_;
    Dialect dialect_;
    Family family_;

    std::vector<View> views_;
    std::vector<const std::string*> macroStack_;  // for the cycle check
    std::size_t diagOffset_ = 0;  // where the command being read started

    std::vector<std::uint8_t> bytes_;
    std::set<int>* adpcmFiles_ = nullptr;

    // running compile state
    int octave_ = 4;
    int defaultLen_ = kTicksQuarter;
    int tupletLen_ = 0;  // 0 when no tuplet is open
    bool noTime_ = false;
    bool voiced_ = false;
    int accent_ = -1;  // the rhythm accent set, -1 when it has to be written

    std::vector<std::size_t> loops_;
    bool groupOpen_ = false;
    bool blockOpen_ = false;
    std::size_t blockNextField_ = 0;
    bool haveBlockNext_ = false;
    std::vector<std::size_t> blockEndFields_;

    std::array<std::size_t, kSegnoMax> segno_{};
    std::array<bool, kSegnoMax> segnoSeen_{};
    bool codaSeen_ = false;
    std::size_t codaPos_ = 0;
    std::vector<std::size_t> toCodaFields_;
    int marks_ = 0;
};

// ---------------------------------------------------------------------------
// reader
// ---------------------------------------------------------------------------

// Spaces, tabs and the line breaks that join a track's lines are skipped
// wherever they fall, so "| :" and "L1 6" read as "|:" and "L16" do.
bool TrackCompiler::peek(char& c) {
    while (!views_.empty()) {
        View& v = views_.back();
        while (v.pos < v.text->size()) {
            char ch = (*v.text)[v.pos];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                ++v.pos;
                continue;
            }
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return true;
        }
        if (views_.size() == 1) return false;
        views_.pop_back();
        macroStack_.pop_back();
    }
    return false;
}

void TrackCompiler::skip() {
    char c;
    if (peek(c)) ++views_.back().pos;
}

bool TrackCompiler::next(char& c) {
    if (!peek(c)) return false;
    ++views_.back().pos;
    return true;
}

// ---------------------------------------------------------------------------
// diagnostics
// ---------------------------------------------------------------------------

void TrackCompiler::failAt(std::size_t offset, const std::string& message) {
    int line = 0, column = 0;
    track_.locate(offset, line, column);
    diag_.error(src_.path, line, column, message);
    throw Fail{};
}

void TrackCompiler::fail(const std::string& message) { failAt(diagOffset_, message); }

// ---------------------------------------------------------------------------
// numbers and lengths
// ---------------------------------------------------------------------------

bool TrackCompiler::getNum(long& out) {
    char c;
    if (!peek(c)) return false;

    if (c == '$') {
        // Always two digits: a-f is a note letter as well as a hex digit, so a
        // number that ran to the first non-digit would swallow the note after it.
        skip();
        long v = 0;
        for (int i = 0; i < 2; ++i) {
            char d;
            if (!peek(d)) fail("$ takes two hexadecimal digits");
            int digit;
            if (d >= '0' && d <= '9') {
                digit = d - '0';
            } else if (d >= 'a' && d <= 'f') {
                digit = d - 'a' + 10;
            } else {
                fail("$ takes two hexadecimal digits");
            }
            v = v * 16 + digit;
            skip();
        }
        out = v;
        return true;
    }

    if (c == '=') {
        skip();
        std::string name;
        char d;
        while (peek(d) && d != ';') {
            name.push_back((*views_.back().text)[views_.back().pos]);
            skip();
        }
        if (!peek(d)) fail("=" + name + " has no ';' to close it");
        skip();
        auto it = src_.macros.find(name);
        if (it == src_.macros.end()) fail("'" + name + "' is not defined");
        if (it->second.isString) fail("'" + name + "' is a string, not a number");
        out = it->second.number;
        return true;
    }

    if (c < '0' || c > '9') return false;
    long v = 0;
    while (peek(c) && c >= '0' && c <= '9') {
        v = v * 10 + (c - '0');
        if (v > 65535) fail("a number here is at most 65535");
        skip();
    }
    out = v;
    return true;
}

long TrackCompiler::needNum() {
    long v = 0;
    if (!getNum(v)) fail("a number was expected here");
    return v;
}

long TrackCompiler::getSigned() {
    char c;
    if (peek(c) && (c == '-' || c == '+')) {
        bool neg = (c == '-');
        skip();
        long v = needNum();
        return neg ? -v : v;
    }
    return needNum();
}

long TrackCompiler::optNum(long def) {
    long v = 0;
    return getNum(v) ? v : def;
}

int TrackCompiler::dots(int base) {
    int total = base;
    int d = base;
    char c;
    while (peek(c) && c == '.') {
        skip();
        d >>= 1;
        total += d;
    }
    return total;
}

// The length a note carries: its own if it wrote one, else the tuplet's share,
// else what Ln set. A length of 0 is a note that takes no time.
int TrackCompiler::getLen() {
    long n = 0;
    if (!getNum(n)) {
        return dots(tupletLen_ != 0 ? tupletLen_ : defaultLen_);
    }
    if (n == 0) return 0;  // the dots of a zero length add nothing
    if (n > kLengthMax) fail("a length is 1 to " + std::to_string(kLengthMax));
    return dots(kTicksWhole / static_cast<int>(n));
}

void TrackCompiler::emitLen(int ticks) {
    if (ticks < 128) {
        emit(static_cast<std::uint8_t>(ticks));
    } else {
        emit(static_cast<std::uint8_t>(0x80 | ((ticks >> 8) & 0x7F)));
        emit(static_cast<std::uint8_t>(ticks & 0xFF));
    }
}

// A distance is counted from where the player stands once it has read the two
// bytes, which is just past the field.
void TrackCompiler::patch(std::size_t field, std::size_t target) {
    int delta = static_cast<int>(target) - static_cast<int>(field + 2);
    bytes_[field] = static_cast<std::uint8_t>(delta & 0xFF);
    bytes_[field + 1] = static_cast<std::uint8_t>((delta >> 8) & 0xFF);
}

// ---------------------------------------------------------------------------
// voices
// ---------------------------------------------------------------------------

void TrackCompiler::voiceNumber(long n) {
    if (dialect_ == Dialect::Adpcm) {
        if (n > 31) fail("a voice file number is 0 to 31");
        adpcmFiles_->insert(static_cast<int>(n));
        emit(OpVoice, static_cast<std::uint8_t>(n));
        voiced_ = true;
        return;
    }

    if (family_ == Family::Fm) {
        if (n > kVoiceUserLast) fail("@n on an FM channel is 0 to 191");
        if (n == kOpllBanked) fail("@64 names no voice: preset 0 of every bank is the user voice");
        if (n >= kVoiceUserFirst) {
            fail("@" + std::to_string(n) +
                 " is a voice VOICE COPY makes, which this compiler cannot build yet");
        }
        if (n > kOpllBanked) {
            // OPLLEX's own presets, bank and number packed into one byte. No
            // record could stand for them, so the number goes as it is; OPL2EX
            // has no such bank and its driver drops the event.
            emit(OpVoice, static_cast<std::uint8_t>(n));
            voiced_ = true;
            return;
        }
        int slot = voices_.intern(false, static_cast<int>(n), presetVoice(static_cast<int>(n)));
        if (slot < 0) fail("this sequence already carries " + std::to_string(kVoiceSlots) + " voices");
        emit(OpSeqVoice, static_cast<std::uint8_t>(slot));
        voiced_ = true;
        return;
    }

    // The PSG family: an SCC waveform is a record, everything else is a number
    // the chip resolves by itself.
    if (n > 255) fail("@n is 0 to 255");
    if (track_.device == DevSCC && n <= kWaveUserLast) {
        if (n >= kWaveUserFirst) {
            fail("@" + std::to_string(n) +
                 " is a waveform WAVE COPY makes, which this compiler cannot build yet");
        }
        int slot = voices_.intern(true, static_cast<int>(n), presetWave(static_cast<int>(n)));
        if (slot < 0) fail("this sequence already carries " + std::to_string(kVoiceSlots) + " voices");
        emit(OpSeqVoice, static_cast<std::uint8_t>(slot));
    } else {
        emit(OpVoice, static_cast<std::uint8_t>(n));
    }
    voiced_ = true;
}

// A note written before the first @n plays voice 0. Where a record stands
// behind that number, the record goes into the sequence and the track names its
// slot, so the block is closed about its voices.
void TrackCompiler::defaultVoice() {
    if (voiced_) return;
    voiced_ = true;
    if (dialect_ == Dialect::Adpcm) {
        adpcmFiles_->insert(0);
        return;
    }
    if (family_ == Family::Fm) {
        int slot = voices_.intern(false, 0, presetVoice(0));
        if (slot < 0) fail("this sequence already carries " + std::to_string(kVoiceSlots) + " voices");
        emit(OpSeqVoice, static_cast<std::uint8_t>(slot));
    } else if (track_.device == DevSCC) {
        int slot = voices_.intern(true, 0, presetWave(0));
        if (slot < 0) fail("this sequence already carries " + std::to_string(kVoiceSlots) + " voices");
        emit(OpSeqVoice, static_cast<std::uint8_t>(slot));
    }
}

// ---------------------------------------------------------------------------
// notes
// ---------------------------------------------------------------------------

void TrackCompiler::timeFlag(int ticks) { noTime_ = (ticks == 0); }

void TrackCompiler::noteEmit(std::uint8_t op, int ticks) {
    // Two key-ons cannot fall on the same tick: the second would only overwrite
    // the first. A length of 0 is what makes that possible to write.
    if (noTime_) fail("two notes with no time between them");
    defaultVoice();
    emit(op);
    emitLen(ticks);
    timeFlag(ticks);
}

void TrackCompiler::cmdNote(char c) {
    int semi = kSemitone[c - 'a'];
    char a;
    if (peek(a) && (a == '+' || a == '#')) {
        skip();
        semi = (semi + 1) % 12;
    } else if (peek(a) && a == '-') {
        skip();
        semi = (semi + 11) % 12;
    }
    int ticks = getLen();
    noteEmit(static_cast<std::uint8_t>(OpNote + semi), ticks);
}

void TrackCompiler::cmdNoteAbs() {
    long n = needNum();
    if (n > kNoteNumberMax) fail("Nn is 0 to " + std::to_string(kNoteNumberMax));
    int ticks = getLen();
    if (noTime_) fail("two notes with no time between them");
    defaultVoice();
    emit(OpNoteAbs, static_cast<std::uint8_t>(n));
    emitLen(ticks);
    timeFlag(ticks);
}

void TrackCompiler::cmdRest() {
    int ticks = getLen();
    emit(OpRest);
    emitLen(ticks);
    timeFlag(ticks);
}

// ---------------------------------------------------------------------------
// the running state commands
// ---------------------------------------------------------------------------

void TrackCompiler::cmdOctave() {
    long n = needNum();
    if (n < 1 || n > kOctaveMax) fail("On is 1 to " + std::to_string(kOctaveMax));
    octave_ = static_cast<int>(n);
    emit(OpOctave, static_cast<std::uint8_t>(n));
}

// At the top it stays there: a part that walks off the end for a bar is not a
// part with an error in it.
void TrackCompiler::cmdOctUp() {
    if (octave_ >= kOctaveMax) return;
    ++octave_;
    emit(OpOctUp);
}

void TrackCompiler::cmdOctDown() {
    if (octave_ <= 1) return;
    --octave_;
    emit(OpOctDown);
}

void TrackCompiler::cmdLength() {
    long n = needNum();
    if (n < 1 || n > kLengthMax) fail("Ln is 1 to " + std::to_string(kLengthMax));
    defaultLen_ = dots(kTicksWhole / static_cast<int>(n));
}

void TrackCompiler::cmdVolume() {
    long n = needNum();
    if (n > kVolMax) fail("Vn is 0 to " + std::to_string(kVolMax));
    // n*8+7 is the scaling a four bit chip takes the top of and gets n back.
    emit(OpVolume, static_cast<std::uint8_t>(n * 8 + 7));
}

void TrackCompiler::cmdTempo() {
    long n = needNum();
    if (n < kTempoMin || n > 255) fail("Tn is " + std::to_string(kTempoMin) + " to 255");
    emit(OpTempo, static_cast<std::uint8_t>(n));
}

void TrackCompiler::cmdQuantize() {
    long n = needNum();
    if (n < 1 || n > kQuantMax) fail("Qn is 1 to " + std::to_string(kQuantMax));
    emit(OpQuantize, static_cast<std::uint8_t>(n));
}

// S, M and I are the PSG family's. They are taken on either family - a part
// written for one device and played on another should not stop compiling - and
// the driver of a device with no envelope drops them.
void TrackCompiler::cmdShape() {
    long n = needNum();
    if (n > 15) fail("Sn is 0 to 15");
    emit(OpSsgShape, static_cast<std::uint8_t>(n));
}

void TrackCompiler::cmdPeriod() {
    long n = needNum();
    if (n < 1 || n > 65535) fail("Mn is 1 to 65535");
    emit(OpSsgPeriod);
    emitWord(static_cast<std::uint16_t>(n));
}

void TrackCompiler::cmdPan() {
    long n = needNum();
    if (n > 15) fail("In is 0 to 15");
    emit(OpSsgPan, static_cast<std::uint8_t>(n));
}

void TrackCompiler::cmdBend(std::uint8_t op) {
    long n = getSigned();
    if (n < -kCentMax || n > kCentMax) {
        fail("cents are -" + std::to_string(kCentMax) + " to " + std::to_string(kCentMax));
    }
    emit(op);
    emitWord(static_cast<std::uint16_t>(n));
}

void TrackCompiler::cmdPorta() {
    char c;
    long n = 0;
    bool have = false;
    if (peek(c) && (c == '-' || c == '+' || (c >= '0' && c <= '9') || c == '$' || c == '=')) {
        n = getSigned();
        have = true;
    }
    if (!have) {
        emit(OpPorta);
        emitWord(kPortaFromHere);
        return;
    }
    if (n < -kCentMax || n > kCentMax) {
        fail("cents are -" + std::to_string(kCentMax) + " to " + std::to_string(kCentMax));
    }
    emit(OpPorta);
    emitWord(static_cast<std::uint16_t>(n));
}

void TrackCompiler::cmdReg() {
    long reg = needNum();
    if (reg > 255) fail("a register number is 0 to 255");
    char c;
    if (!peek(c) || c != ',') fail("Y takes a register and a value, separated by ','");
    skip();
    long data = needNum();
    if (data > 255) fail("a register value is 0 to 255");
    long mask = 0;  // no mask: keep nothing
    if (peek(c) && c == ',') {
        skip();
        mask = needNum();
        if (mask > 255) fail("a register mask is 0 to 255");
    }
    emit(OpRegWrite, static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(data));
    emit(static_cast<std::uint8_t>(mask));
}

void TrackCompiler::cmdAt() {
    char c;
    if (!peek(c)) fail("@ has nothing after it");
    if (c == 'v') {
        skip();
        long n = needNum();
        if (n > kMixerMax) fail("@Vn is 0 to " + std::to_string(kMixerMax));
        emit(OpVolume, static_cast<std::uint8_t>(n));
        return;
    }
    if (c == 'w') {
        skip();
        int ticks = getLen();
        if (ticks == 0) fail("@W0 waits for nothing");
        emit(OpWait);
        emitLen(ticks);
        timeFlag(ticks);
        return;
    }
    if (c == 'p') {
        skip();
        cmdBend(OpBendRel);
        return;
    }
    voiceNumber(needNum());
}

// ---------------------------------------------------------------------------
// tuplets
// ---------------------------------------------------------------------------

// Counts the notes between here and the matching '}' without moving the cursor
// on. The share has to be known before the first note is written, and nothing
// else in the text says how many are coming.
int TrackCompiler::tupletCount() {
    std::vector<View> saved = views_;
    std::vector<const std::string*> savedMacros = macroStack_;
    int count = 0;
    char c;
    bool closed = false;
    while (next(c)) {
        if (c == '}') {
            closed = true;
            break;
        }
        if (c == '{') break;  // tuplets do not nest
        if (c == '$') {
            // a-f is a hex digit here, not a note
            char d;
            if (!next(d)) break;
            if (!next(d)) break;
            continue;
        }
        if (c == 'n' || c == 'r' || (c >= 'a' && c <= 'g')) ++count;
    }
    views_ = saved;
    macroStack_ = savedMacros;
    if (!closed) fail("a '{' with no '}' to close it");
    return count;
}

void TrackCompiler::cmdTupletStart() {
    if (tupletLen_ != 0) fail("tuplets do not nest");
    int notes = tupletCount();
    if (notes == 0) fail("a tuplet with no notes in it");

    std::vector<View> saved = views_;
    std::vector<const std::string*> savedMacros = macroStack_;
    // Walk to the '}' again to read the length written after it.
    char c;
    while (next(c) && c != '}') {
        if (c == '$') {
            char d;
            if (!next(d)) break;
            if (!next(d)) break;
        }
    }
    int share = getLen() / notes;
    views_ = saved;
    macroStack_ = savedMacros;

    // Under two ticks is where notes * length passes the length limit, which is
    // what the MML this follows refuses.
    if (share < 2) fail("this tuplet divides below the smallest length");
    tupletLen_ = share;
}

void TrackCompiler::cmdTupletEnd() {
    if (tupletLen_ == 0) fail("a '}' with no '{' to open it");
    tupletLen_ = 0;
    getLen();  // reading it again is how the cursor gets past it
}

void TrackCompiler::cmdXString() {
    std::string name;
    char c;
    while (peek(c) && c != ';') {
        name.push_back((*views_.back().text)[views_.back().pos]);
        skip();
    }
    if (!peek(c)) fail("X" + name + " has no ';' to close it");
    skip();
    auto it = src_.macros.find(name);
    if (it == src_.macros.end()) fail("'" + name + "' is not defined");
    if (!it->second.isString) fail("'" + name + "' is a number, not MML");
    if (static_cast<int>(macroStack_.size()) >= kXChainMax) {
        fail("X" + name + " goes more than " + std::to_string(kXChainMax) + " deep");
    }
    views_.push_back({&it->second.text, 0, true});
    macroStack_.push_back(&it->second.text);
}

// ---------------------------------------------------------------------------
// rhythm
// ---------------------------------------------------------------------------

namespace {
std::uint8_t rhythmBit(char c) {
    switch (c) {
        case 'b': return kRhythmBass;
        case 's': return kRhythmSnare;
        case 'm': return kRhythmTom;
        case 'c': return kRhythmCymbal;
        case 'h': return kRhythmHiHat;
        default: return 0;
    }
}
} // namespace

// A run of instrument letters and the length that sounds them. A '!' after a
// letter puts that one on the accent level.
void TrackCompiler::cmdRhythmInstruments(char first) {
    std::uint8_t hit = 0;
    std::uint8_t accent = 0;
    char c = first;
    for (;;) {
        std::uint8_t bit = rhythmBit(c);
        hit |= bit;
        char a;
        if (peek(a) && a == '!') {
            skip();
            accent |= bit;
        }
        if (!peek(a)) fail("a run of rhythm instruments needs a length after it");
        if (rhythmBit(a) == 0) break;
        skip();
        c = a;
    }
    int ticks = getLen();
    if (accent_ != accent) {
        accent_ = accent;
        emit(OpRhythmAccent, accent);
    }
    emit(OpRhythmHit, hit);
    emitLen(ticks);
    timeFlag(ticks);
}

void TrackCompiler::cmdRhythmVolume() {
    long n = needNum();
    if (n > kRhyVolMax) fail("Vn on a rhythm track is 0 to " + std::to_string(kRhyVolMax));
    emit(OpRhythmVolume, static_cast<std::uint8_t>(n));
}

void TrackCompiler::cmdRhythmAt() {
    char c;
    if (!peek(c)) fail("@ has nothing after it");
    if (c == 'v') {
        skip();
        long n = needNum();
        if (n > kMixerMax) fail("@Vn is 0 to " + std::to_string(kMixerMax));
        emit(OpRhythmVolume, static_cast<std::uint8_t>((n >> 3) & kRhyVolMax));
        return;
    }
    if (c == 'a') {
        skip();
        long n = needNum();
        if (n > kRhyVolMax) fail("@An is 0 to " + std::to_string(kRhyVolMax));
        emit(OpRhythmAccentVol, static_cast<std::uint8_t>(n));
        return;
    }
    fail("@ names nothing else on a rhythm track");
}

// ---------------------------------------------------------------------------
// loops, blocks and the marks
// ---------------------------------------------------------------------------

// A group whose last block was closed with ']' ends at the next command, unless
// that command is another '['. So what ends it is the command after it, and the
// parser has to ask before every one.
void TrackCompiler::blockAuto(char nextCommand) {
    if (nextCommand == '[') return;
    if (groupOpen_ && !blockOpen_) blockClose();
}

void TrackCompiler::blockEnd() {
    if (!blockOpen_) return;
    emit(OpBlockEnd);
    blockEndFields_.push_back(bytes_.size());
    emitWord(0);
    blockOpen_ = false;
}

void TrackCompiler::blockClose() {
    accent_ = -1;
    blockEnd();
    groupOpen_ = false;
    haveBlockNext_ = false;
    std::size_t end = bytes_.size();
    for (std::size_t field : blockEndFields_) patch(field, end);
    blockEndFields_.clear();
}

void TrackCompiler::cmdLoopStart() {
    accent_ = -1;
    char c;
    if (!peek(c) || c != ':') fail("'|' is only the first half of '|:'");
    skip();
    // A loop head inside an open group would have its tail end that group, and
    // the group's blocks would then be filled in against the wrong place.
    if (groupOpen_) fail("'|:' cannot be written inside a block group");
    if (static_cast<int>(loops_.size()) >= kLoopDepth) {
        fail("loops nest at most " + std::to_string(kLoopDepth) + " deep");
    }
    emit(OpLoopStart);
    loops_.push_back(bytes_.size());
}

void TrackCompiler::cmdLoopEnd() {
    accent_ = -1;
    char c;
    if (!peek(c) || c != '|') fail("':' is only the first half of ':|'");
    skip();
    blockClose();  // a block runs to the tail, so the tail ends its group
    if (loops_.empty()) fail("':|' with no '|:' to go back to");
    std::size_t body = loops_.back();
    loops_.pop_back();
    long count = optNum(1);
    if (count > 255) fail("a loop count is 0 to 255");
    emit(OpLoopEnd, static_cast<std::uint8_t>(count));
    std::size_t field = bytes_.size();
    emitWord(0);
    patch(field, body);
}

void TrackCompiler::cmdBlockStart() {
    accent_ = -1;
    // The number is read before anything is written, so a bad one leaves the
    // track alone. It is not optional: it is what tells the number and the
    // block's first command apart.
    long iteration = needNum();
    if (iteration > 255) fail("a block's iteration is 0 to 255");
    blockEnd();
    if (groupOpen_ && haveBlockNext_) patch(blockNextField_, bytes_.size());
    emit(OpBlockStart, static_cast<std::uint8_t>(iteration));
    blockNextField_ = bytes_.size();
    haveBlockNext_ = true;
    emitWord(0);  // the last block of a group keeps this zero
    groupOpen_ = true;
    blockOpen_ = true;
}

void TrackCompiler::cmdBlockEnd() {
    if (!blockOpen_) fail("']' with no '[' to open a block");
    blockEnd();
}

void TrackCompiler::cmdParen() {
    char c;
    if (!next(c)) fail("'(' has nothing after it");

    auto expect = [&](const char* rest) {
        for (const char* p = rest; *p; ++p) {
            char d;
            if (!next(d) || d != *p) fail("this is not one of the bracketed commands");
        }
        char d;
        if (!next(d) || d != ')') fail("this bracketed command has no ')'");
    };

    if (c == '*') {
        expect("");
        long n = optNum(0);
        if (n >= kSegnoMax) fail("a segno number is 0 to " + std::to_string(kSegnoMax - 1));
        accent_ = -1;
        // A segno writes nothing: it is a place, and only a (DS) wants it.
        segno_[n] = bytes_.size();
        segnoSeen_[n] = true;
        return;
    }
    if (c == 't') {
        expect("c");
        long n = optNum(2);
        if (n > 255) fail("a (TC) count is 0 to 255");
        if (marks_ >= kMarkMax) {
            fail("a track holds at most " + std::to_string(kMarkMax) + " of (TC) and (FINE)");
        }
        int mark = marks_++;
        emit(OpToCoda, static_cast<std::uint8_t>(mark), static_cast<std::uint8_t>(n));
        std::size_t field = bytes_.size();
        emitWord(0);
        if (codaSeen_) {
            patch(field, codaPos_);
        } else {
            toCodaFields_.push_back(field);
        }
        return;
    }
    if (c == 'c') {
        expect("oda");
        if (codaSeen_) fail("a track has one (CODA)");
        accent_ = -1;
        emit(OpCoda);
        codaPos_ = bytes_.size();
        codaSeen_ = true;
        for (std::size_t field : toCodaFields_) patch(field, codaPos_);
        toCodaFields_.clear();
        return;
    }
    if (c == 'f') {
        expect("ine");
        long n = optNum(2);
        if (n > 255) fail("a (FINE) count is 0 to 255");
        if (marks_ >= kMarkMax) {
            fail("a track holds at most " + std::to_string(kMarkMax) + " of (TC) and (FINE)");
        }
        int mark = marks_++;
        emit(OpFine, static_cast<std::uint8_t>(mark), static_cast<std::uint8_t>(n));
        return;
    }
    if (c == 'd') {
        char d;
        if (!next(d)) fail("'(D' has nothing after it");
        if (d == 'c') {
            expect("");
            emit(OpDaCapo);
            return;
        }
        if (d == 's') {
            expect("");
            long n = optNum(0);
            if (n >= kSegnoMax) fail("a segno number is 0 to " + std::to_string(kSegnoMax - 1));
            emit(OpDalSegno);
            std::size_t field = bytes_.size();
            emitWord(0);
            // A dal segno always jumps back, so one written before its (*)
            // names nothing - and a distance of zero is what "ignored" is.
            if (segnoSeen_[n]) patch(field, segno_[n]);
            return;
        }
        fail("this is not one of the bracketed commands");
    }
    fail("this is not one of the bracketed commands");
}

// ---------------------------------------------------------------------------
// dispatch
// ---------------------------------------------------------------------------

void TrackCompiler::melodyCommand(char c) {
    switch (c) {
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
            cmdNote(c); return;
        case 'r': cmdRest(); return;
        case 'n': cmdNoteAbs(); return;
        case 'o': cmdOctave(); return;
        case '>': cmdOctUp(); return;
        case '<': cmdOctDown(); return;
        case 'l': cmdLength(); return;
        case 'v': cmdVolume(); return;
        case '@': cmdAt(); return;
        case 't': cmdTempo(); return;
        case 'q': cmdQuantize(); return;
        case '&': emit(OpTie); return;
        case 's': cmdShape(); return;
        case 'm': cmdPeriod(); return;
        case 'i': cmdPan(); return;
        case 'p': cmdBend(OpBend); return;
        case '~': cmdPorta(); return;
        case 'y': cmdReg(); return;
        case '{': cmdTupletStart(); return;
        case '}': cmdTupletEnd(); return;
        case 'x': cmdXString(); return;
        default: break;
    }
    fail(std::string("'") + c + "' is not an MML command");
}

void TrackCompiler::rhythmCommand(char c) {
    switch (c) {
        case 'b': case 's': case 'm': case 'c': case 'h':
            cmdRhythmInstruments(c); return;
        case 'r': cmdRest(); return;
        case 't': cmdTempo(); return;
        case 'v': cmdRhythmVolume(); return;
        case '@': cmdRhythmAt(); return;
        case 'y': cmdReg(); return;
        case 'x': cmdXString(); return;
        default: break;
    }
    fail(std::string("'") + c + "' is not a rhythm MML command");
}

void TrackCompiler::dispatch(char c) {
    switch (c) {
        case '|': cmdLoopStart(); return;
        case ':': cmdLoopEnd(); return;
        case '[': cmdBlockStart(); return;
        case ']': cmdBlockEnd(); return;
        case '(': cmdParen(); return;
        default: break;
    }
    if (dialect_ == Dialect::Rhythm) {
        rhythmCommand(c);
    } else {
        melodyCommand(c);
    }
}

bool TrackCompiler::run(TrackCode& out, std::set<int>& adpcmVoiceFiles) {
    adpcmFiles_ = &adpcmVoiceFiles;
    views_.push_back({&track_.text, 0, false});

    try {
        char c;
        while (peek(c)) {
            if (views_.size() == 1) diagOffset_ = views_.back().pos;
            blockAuto(c);
            skip();
            dispatch(c);
        }

        if (!loops_.empty()) fail("a '|:' is still open at the end of the track");
        if (groupOpen_ || blockOpen_) fail("a block group is still open at the end of the track");
        if (tupletLen_ != 0) fail("a '{' is still open at the end of the track");
        // A (TC) with no (CODA) anywhere in the track is ignored, which is what
        // a distance of zero says.
        for (std::size_t field : toCodaFields_) {
            bytes_[field] = 0;
            bytes_[field + 1] = 0;
        }
    } catch (const Fail&) {
        return false;
    }

    emit(OpEnd);
    if (bytes_.size() > static_cast<std::size_t>(kTrackBytesMax)) {
        int line = 0, column = 0;
        track_.locate(track_.text.size(), line, column);
        diag_.error(src_.path, line, column,
                    "this track is " + std::to_string(bytes_.size()) + " bytes; the limit is " +
                        std::to_string(kTrackBytesMax));
        return false;
    }

    out.bytes = std::move(bytes_);
    return true;
}

} // namespace

bool compileSequence(const SourceFile& src, Sequence& out, Diagnostics& diag) {
    bool ok = true;
    for (int i = 0; i < kTrackCount; ++i) {
        const TrackSource& t = src.tracks[i];
        if (!t.assigned) {
            if (!t.text.empty()) {
                int line = 0, column = 0;
                t.locate(0, line, column);
                diag.error(src.path, line, column,
                           std::string("track ") + static_cast<char>('A' + i) +
                               " has MML but no #assign");
                ok = false;
            }
            continue;
        }

        TrackCode& code = out.tracks[i];
        code.assigned = true;
        code.device = t.device;
        code.channel = t.channel;

        if (dialectFor(t.device, t.channel) == Dialect::Rhythm &&
            (t.device == DevOPL2EX1 || t.device == DevOPL2EX2)) {
            // OPL2EX has no rhythm voices of its own, so the sequence carries them.
            out.voices.needRhythmVoices();
        }

        TrackCompiler compiler(src, i, out.voices, diag);
        if (!compiler.run(code, out.adpcmVoiceFiles)) {
            ok = false;
            code.bytes.assign(1, OpEnd);
        }
    }
    return ok;
}

} // namespace y8
