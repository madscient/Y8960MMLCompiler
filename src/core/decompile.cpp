#include "decompile.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>

#include "device.h"
#include "mml.h"
#include "opcodes.h"
#include "voicedata.h"

namespace y8 {
namespace {

constexpr std::uint8_t kChunkTrack = 0x00;
constexpr std::uint8_t kChunkVoice = 0x01;
constexpr std::uint8_t kChunkWave = 0x02;
constexpr std::uint8_t kChunkVoiceFile = 0x03;
constexpr std::uint8_t kChunkEnvelope = 0x04;
constexpr std::uint8_t kChunkDeviceOwned = 0x40;  // 40-7F: the body's first byte names a device
constexpr std::uint8_t kChunkSkippable = 0x80;
constexpr int kSlotIndexLimit = 35;  // 0-31 the set, 32-34 OPL2EX's rhythm voices
constexpr int kOpllBanked = 64;      // @65-@127 is 82 with @n less this

// Where the records of the voice set land in the source. #voice has 64 numbers
// and the set 32 slots, so every FM record fits; #wave has 16.
constexpr int kUserVoiceBase = 128;
constexpr int kUserWaveBase = 16;
constexpr int kUserWaveCount = 16;

constexpr int kMaxDots = 7;         // past this a whole note's dot adds nothing
constexpr int kLengthTicksMax = 382;  // a whole note with every dot that adds
constexpr int kPieceMax = 336;      // the longest note MML makes, a double-dotted whole
constexpr std::size_t kLineWidth = 76;

const char* const kNoteNames[12] = {"c", "c+", "d", "d+", "e", "f",
                                    "f+", "g", "g+", "a", "a+", "b"};

// The tick counts one note can carry, written the way getLen reads them: 192/n
// cut down, then each dot adding half of what the last one added. A count has
// the form with the fewest dots, and of those the smallest n. Most counts have
// no form at all - a note of one of those becomes tied notes.
class Lengths {
public:
    Lengths() {
        for (int n = 1; n <= kLengthMax; ++n) {
            int total = kTicksWhole / n;
            int add = total;
            for (int dots = 0; dots <= kMaxDots; ++dots) {
                Form& f = forms_[static_cast<std::size_t>(total)];
                if (f.den == 0 || dots < f.dots) f = {n, dots};
                add >>= 1;
                if (add == 0) break;
                total += add;
            }
        }
    }

    bool has(int ticks) const {
        return ticks == 0 ||
               (ticks > 0 && ticks <= kLengthTicksMax && forms_[static_cast<std::size_t>(ticks)].den != 0);
    }

    std::string text(int ticks) const {
        if (ticks == 0) return "0";
        const Form& f = forms_[static_cast<std::size_t>(ticks)];
        return std::to_string(f.den) + std::string(static_cast<std::size_t>(f.dots), '.');
    }

    // Splits a count into ones that have a form, largest first. Two and three
    // ticks both have one, so any count of two or more can be split; the caller
    // has already dealt with one.
    std::vector<int> pieces(int ticks) const {
        std::vector<int> out;
        while (!(ticks <= kPieceMax && has(ticks))) {
            int p = std::min(ticks - 2, kPieceMax);
            while (!has(p)) --p;
            out.push_back(p);
            ticks -= p;
        }
        out.push_back(ticks);
        return out;
    }

private:
    struct Form {
        int den = 0;
        int dots = 0;
    };
    std::array<Form, kLengthTicksMax + 1> forms_{};
};

const Lengths& lengths() {
    static const Lengths table;
    return table;
}

struct Event {
    std::size_t at = 0;   // where it starts in the event stream
    std::size_t end = 0;  // just past it, which is where its distances count from
    std::uint8_t op = 0;
    std::array<std::uint8_t, 4> arg{};
    int len = -1;         // the length, for the opcodes that carry one

    unsigned word(int i) const { return arg[static_cast<std::size_t>(i)] | (arg[static_cast<std::size_t>(i) + 1] << 8); }
    long signedWord(int i) const { return static_cast<std::int16_t>(word(i)); }
    std::size_t target(int i) const {
        return static_cast<std::size_t>(static_cast<long>(end) + signedWord(i));
    }
};

bool decodeTrack(const std::vector<std::uint8_t>& b, std::vector<Event>& out, std::string& error) {
    std::size_t i = 0;
    while (i < b.size()) {
        Event e;
        e.at = i;
        e.op = b[i++];
        if (e.op == OpEnd) {
            e.end = i;
            out.push_back(e);
            return true;
        }
        int argc = 0;
        bool hasLen = false;
        if (e.op < 0x40) {
            hasLen = true;
        } else if (e.op < 0x80) {
            argc = 0;
        } else if (e.op < 0xC0) {
            argc = 1;
        } else if (e.op < 0xD0) {
            argc = 1;
            hasLen = true;
        } else if (e.op < 0xE0) {
            argc = 2;
        } else if (e.op < 0xF0) {
            argc = 3;
        } else {
            argc = 4;
        }
        if (i + static_cast<std::size_t>(argc) > b.size()) break;
        for (int k = 0; k < argc; ++k) e.arg[static_cast<std::size_t>(k)] = b[i++];
        if (hasLen) {
            if (i >= b.size()) break;
            int first = b[i++];
            if (first & 0x80) {
                if (i >= b.size()) break;
                e.len = ((first & 0x7F) << 8) | b[i++];
            } else {
                e.len = first;
            }
        }
        e.end = i;
        out.push_back(e);
    }
    error = "the event stream stops before its FF";
    return false;
}

struct TrackChunk {
    bool present = false;
    Device device = DevSSGS;
    int channel = 0;
    std::vector<std::uint8_t> bytes;
};

struct Slot {
    bool present = false;
    bool isWave = false;   // chunk 02 rather than chunk 01
    VoiceRecord record{};  // a voice's 12 bytes lead, as chunk 01 has them
};

// What the tracks share: the voice numbers they have handed out, the envelopes
// and voice files they name.
struct Shared {
    std::array<Slot, kVoiceSlots> slots;
    std::map<int, EnvRecord> envelopes;

    std::map<int, int> voiceOfSlot;  // FM: slot -> the @n its #voice has
    std::map<int, int> waveOfSlot;   // SCC: slot -> the @n its #wave or preset has
    std::vector<std::pair<int, VoiceRecord>> voiceDefs;
    std::vector<std::pair<int, VoiceRecord>> waveDefs;
    std::set<int> envUsed;
    std::set<int> filesSounded;
    bool zeroMacro = false;  // "=Z;", the one way to write N with length 0
};

class TrackWriter {
public:
    TrackWriter(int index, const TrackChunk& chunk, Shared& shared, const DecompileOptions& opt,
                Diagnostics& diag)
        : index_(index), chunk_(chunk), shared_(shared), opt_(opt), diag_(diag) {
        dialect_ = dialectFor(chunk.device, chunk.channel);
        family_ = deviceFamily(chunk.device);
    }

    // The MML for the track, as tokens. False when the stream cannot be read.
    bool run(std::vector<std::string>& tokens);

private:
    void warn(const Event& e, const std::string& message) {
        diag_.warning(opt_.name, 0, 0,
                      std::string("track ") + static_cast<char>('A' + index_) + ", event byte " +
                          std::to_string(e.at) + ": " + message);
    }
    void put(std::string t) { tokens_->push_back(std::move(t)); }

    void event(std::size_t i);
    void melody(std::size_t i);
    void rhythm(std::size_t i);
    bool common(std::size_t i);  // false when the opcode is not one of these

    int settle(const Event& e, int ticks);
    void note(std::size_t i, const std::string& pitch, int noteNumber);
    std::string noteToken(const std::string& pitch, int noteNumber, int ticks);
    void rest(const Event& e, int ticks);
    void wait(const Event& e, int ticks);
    void hit(const Event& e, int ticks);
    void voice(const Event& e);
    void seqVoice(const Event& e);
    void merge() { qUncertain_ = qVaries_; }

    int index_;
    const TrackChunk& chunk_;
    Shared& shared_;
    const DecompileOptions& opt_;
    Diagnostics& diag_;
    Dialect dialect_;
    Family family_;

    std::vector<Event> events_;
    std::vector<std::string>* tokens_ = nullptr;

    // What the compiler will be holding when it reads the tokens so far.
    int octave_ = 4;
    int lengthL_ = kTicksQuarter;
    // What the player will be holding, as far as reading the stream in order
    // tells. Across a loop or a jump it can be something else.
    int quant_ = 8;
    bool qVaries_ = false;
    bool qUncertain_ = false;
    int accent_ = 0;
    bool portaPending_ = false;
    bool voiced_ = false;

    int debt_ = 0;  // ticks written ahead of the stream, from a one tick event
    std::vector<std::size_t> loops_;
    bool blockOpen_ = false;
    std::array<bool, kSegnoMax> segnoSeen_{};
    std::array<std::size_t, kSegnoMax> segnoLast_{};
    std::map<std::size_t, int> segnoAt_;  // just past a segno -> its number
    bool hasCoda_ = false;
    std::size_t codaEnd_ = 0;
    bool codaWritten_ = false;
    int marks_ = 0;
    int accentsDropped_ = 0;  // hits whose A8 named instruments they do not strike
};

bool TrackWriter::run(std::vector<std::string>& tokens) {
    tokens_ = &tokens;
    std::string error;
    if (!decodeTrack(chunk_.bytes, events_, error)) {
        diag_.error(opt_.name, 0, 0,
                    std::string("track ") + static_cast<char>('A' + index_) + ": " + error);
        return false;
    }

    std::set<int> qs = {8};
    for (const Event& e : events_) {
        if (e.op == OpQuantize) qs.insert(e.arg[0]);
        if (e.op == OpCoda && !hasCoda_) {
            hasCoda_ = true;
            codaEnd_ = e.end;
        }
    }
    qVaries_ = qs.size() > 1;

    for (std::size_t i = 0; i < events_.size(); ++i) {
        if (events_[i].op == OpEnd) break;
        event(i);
    }

    const Event& last = events_.back();
    if (blockOpen_) {
        warn(last, "a block is still open at the end; it is closed here");
        put("]");
    }
    while (!loops_.empty()) {
        warn(last, "a loop is still open at the end; it is closed here to play once");
        put(":|1");
        loops_.pop_back();
    }
    if (debt_ > 0) warn(last, "the track ends a tick later than the block's");
    if (accentsDropped_ > 0) {
        // The player sets every instrument's level from A8 at each hit, so an
        // accent bit on one not struck still changes how a ringing one decays.
        // '!' can only mark an instrument that is struck.
        warn(last, std::to_string(accentsDropped_) +
                       " hits carry accents on instruments they do not strike; '!' cannot "
                       "say that, so those accents are dropped");
    }
    return true;
}

void TrackWriter::event(std::size_t i) {
    if (common(i)) return;
    if (dialect_ == Dialect::Rhythm) {
        rhythm(i);
    } else {
        melody(i);
    }
}

// The loops, the marks and the commands both kinds of MML have.
bool TrackWriter::common(std::size_t i) {
    const Event& e = events_[i];
    switch (e.op) {
        case OpLoopStart:
            if (blockOpen_) {
                warn(e, "a loop starts inside a block; the block is closed first");
                put("]");
                blockOpen_ = false;
            }
            loops_.push_back(e.end);
            put("|:");
            merge();
            return true;
        case OpLoopEnd:
            if (loops_.empty()) {
                warn(e, "a loop end with no loop start is left out");
                return true;
            }
            if (e.target(1) != loops_.back()) {
                warn(e, "this loop end goes back somewhere other than its loop start");
            }
            loops_.pop_back();
            put(":|" + std::to_string(e.arg[0]));
            merge();
            return true;
        case OpBlockStart:
            put("[" + std::to_string(e.arg[0]));
            blockOpen_ = true;
            merge();
            return true;
        case OpBlockEnd:
            if (!blockOpen_) {
                warn(e, "a block end with no block open is left out");
                return true;
            }
            put("]");
            blockOpen_ = false;
            merge();
            return true;
        case OpDaCapo:
            put("(dc)");
            return true;
        case OpCoda:
            if (codaWritten_) {
                warn(e, "a second coda is left out: a track has one");
                return true;
            }
            codaWritten_ = true;
            put("(coda)");
            merge();
            return true;
        case OpSegno: {
            int n = e.arg[0];
            if (n >= kSegnoMax) {
                warn(e, "segno " + std::to_string(n) + " is past the last number and is left out");
                return true;
            }
            segnoSeen_[static_cast<std::size_t>(n)] = true;
            segnoLast_[static_cast<std::size_t>(n)] = e.end;
            segnoAt_[e.end] = n;
            put("(*)" + std::to_string(n));
            merge();
            return true;
        }
        case OpDalSegno: {
            if (e.word(0) == 0) {
                // Ignored where it stands. A number with no segno before it
                // compiles to the same nothing.
                for (int n = 0; n < kSegnoMax; ++n) {
                    if (!segnoSeen_[static_cast<std::size_t>(n)]) {
                        put("(ds)" + std::to_string(n));
                        return true;
                    }
                }
                return true;
            }
            auto it = segnoAt_.find(e.target(0));
            if (it == segnoAt_.end()) {
                warn(e, "this dal segno lands on no segno and is left out");
                return true;
            }
            if (segnoLast_[static_cast<std::size_t>(it->second)] != e.target(0)) {
                warn(e, "this dal segno goes to a segno that is not the last of its number; it "
                        "now goes to the last one");
            }
            put("(ds)" + std::to_string(it->second));
            return true;
        }
        case OpToCoda:
            if (e.arg[0] != marks_) warn(e, "this to coda counts on a counter of its own number");
            ++marks_;
            if (e.word(2) == 0 && hasCoda_) {
                warn(e, "a to coda that jumps nowhere, in a track with a coda, is left out");
                return true;
            }
            if (e.word(2) != 0 && e.target(2) != codaEnd_) {
                warn(e, "this to coda lands somewhere other than the coda; it now goes to the coda");
            }
            if (e.arg[1] < kMarkCountMin) {
                warn(e, "a to coda with a count of 0 has no MML and is left out");
                return true;
            }
            put("(tc)" + std::to_string(e.arg[1]));
            return true;
        case OpFine:
            if (e.arg[0] != marks_) warn(e, "this fine counts on a counter of its own number");
            ++marks_;
            if (e.arg[1] < kMarkCountMin) {
                warn(e, "a fine with a count of 0 has no MML and is left out");
                return true;
            }
            put("(fine)" + std::to_string(e.arg[1]));
            return true;
        case OpTempo:
            if (e.arg[0] < 32) {
                warn(e, "tempo " + std::to_string(e.arg[0]) + " is under 32 and is left out");
                return true;
            }
            put("t" + std::to_string(e.arg[0]));
            return true;
        case OpRegWrite: {
            std::string why;
            if (!regWritable(chunk_.device, e.arg[0], e.arg[1], why)) {
                warn(e, "a register write is left out: " + why);
                return true;
            }
            std::string t = "y" + std::to_string(e.arg[0]) + "," + std::to_string(e.arg[1]);
            if (e.arg[2] != 0) t += "," + std::to_string(e.arg[2]);
            put(t);
            return true;
        }
        case OpRest:
            rest(e, e.len);
            return true;
        default:
            return false;
    }
}

void TrackWriter::melody(std::size_t i) {
    const Event& e = events_[i];
    if (e.op < 12) {
        note(i, kNoteNames[e.op], -1);
        return;
    }
    switch (e.op) {
        case OpNoteDown: note(i, "c-", -1); return;
        case OpNoteUp: note(i, "b+", -1); return;
        case OpNoteAbs:
            if (e.arg[0] < kNoteNumberBase || e.arg[0] > kNoteNumberMax + kNoteNumberBase) {
                warn(e, "note number " + std::to_string(e.arg[0]) +
                            " has no N to write it with; a rest takes its time");
                rest(e, e.len);
                return;
            }
            note(i, std::string(), e.arg[0] - kNoteNumberBase);
            return;
        case OpWait: wait(e, e.len); return;
        case OpOctUp:
            if (octave_ >= kOctaveMax) {
                warn(e, "'>' at the top octave is left out");
                return;
            }
            ++octave_;
            put(">");
            return;
        case OpOctDown:
            if (octave_ <= 0) {
                warn(e, "'<' at the bottom octave is left out");
                return;
            }
            --octave_;
            put("<");
            return;
        case OpOctave:
            if (e.arg[0] > kOctaveMax) {
                warn(e, "octave " + std::to_string(e.arg[0]) + " is left out");
                return;
            }
            octave_ = e.arg[0];
            put("o" + std::to_string(e.arg[0]));
            return;
        case OpTie: put("&"); return;
        case OpVolume: {
            int v = e.arg[0];
            if (v > 127) {
                warn(e, "volume " + std::to_string(v) + " is past 127 and is left out");
                return;
            }
            if (v >= kVolumeOfV0 && (v - kVolumeOfV0) % kVolumePerV == 0) {
                put("v" + std::to_string((v - kVolumeOfV0) / kVolumePerV));
            } else {
                put("@v" + std::to_string(v));
            }
            return;
        }
        case OpVoice: voice(e); return;
        case OpSeqVoice: seqVoice(e); return;
        case OpQuantize:
            if (e.arg[0] < 1 || e.arg[0] > 8) {
                warn(e, "Q" + std::to_string(e.arg[0]) + " is left out");
                return;
            }
            quant_ = e.arg[0];
            qUncertain_ = false;
            put("q" + std::to_string(e.arg[0]));
            return;
        case OpBend:
        case OpBendRel:
        case OpPorta: {
            const bool fromHere = e.op == OpPorta && e.word(0) == kPortaFromHere;
            const long cents = e.signedWord(0);
            if (!fromHere && (cents < -kCentMax || cents > kCentMax)) {
                warn(e, std::to_string(cents) + " cents is past the range and is left out");
                return;
            }
            const char* name = e.op == OpBend ? "p" : e.op == OpBendRel ? "@p" : "~";
            put(fromHere ? std::string(name) : name + std::to_string(cents));
            if (e.op == OpPorta) portaPending_ = fromHere || cents != 0;
            return;
        }
        case OpSsgShape:
        case OpPan:
            if (e.arg[0] > 15) {
                warn(e, "a value past 15 is left out");
                return;
            }
            put((e.op == OpSsgShape ? "s" : "i") + std::to_string(e.arg[0]));
            return;
        case OpSsgPeriod:
            if (e.word(0) == 0) {
                warn(e, "an envelope period of 0 is left out");
                return;
            }
            put("m" + std::to_string(e.word(0)));
            return;
        case OpSoftEnv: {
            // The other family's players ignore it, and so does the compiler.
            if (family_ != Family::Psg) return;
            int n = e.arg[0];
            if (n != 0 && shared_.envelopes.find(n) == shared_.envelopes.end()) {
                warn(e, "envelope " + std::to_string(n) + " has no chunk 04 and is left out");
                return;
            }
            if (n != 0) shared_.envUsed.insert(n);
            put("@e" + std::to_string(n));
            return;
        }
        case OpSccVolTable: {
            // Off the SCC the player ignores it, and so does the compiler.
            if (chunk_.device != DevSCC) return;
            int n = e.arg[0];
            if (n > 1) {
                warn(e, "a volume table switch of " + std::to_string(n) + " plays as 0 and is written so");
                n = 0;
            }
            put("@g" + std::to_string(n));
            return;
        }
        case OpRhythmAccent:
            return;  // meaningful on a rhythm channel only
        case OpRhythmHit:
            warn(e, "a rhythm hit on a melody channel becomes a rest");
            rest(e, e.len);
            return;
        default:
            break;
    }
    warn(e, "opcode " + std::to_string(e.op) + " has no MML and is left out");
    if (e.len >= 0) rest(e, e.len);
}

void TrackWriter::rhythm(std::size_t i) {
    const Event& e = events_[i];
    switch (e.op) {
        case OpRhythmHit: hit(e, e.len); return;
        case OpRhythmAccent: accent_ = e.arg[0]; return;
        case OpRhythmVolume:
        case OpRhythmAccentVol:
            if (e.arg[0] > 15) {
                warn(e, "a rhythm volume past 15 is left out");
                return;
            }
            put((e.op == OpRhythmVolume ? "v" : "@a") + std::to_string(e.arg[0]));
            return;
        case OpWait: wait(e, e.len); return;
        case OpRhythmInstVol: {
            const std::uint8_t bits = e.arg[0] & 0x1F;  // bit7-5 mean nothing
            if (bits == 0) return;                       // sets no instrument
            if (e.arg[1] > 15) {
                warn(e, "a rhythm volume past 15 is left out");
                return;
            }
            // One @ per instrument: MML names them one at a time.
            static const struct {
                std::uint8_t bit;
                const char* name;
            } kLevels[] = {{kRhythmBass, "@b"}, {kRhythmSnare, "@s"}, {kRhythmTom, "@m"},
                           {kRhythmCymbal, "@c"}, {kRhythmHiHat, "@h"}};
            for (const auto& k : kLevels) {
                if (bits & k.bit) put(k.name + std::to_string(e.arg[1]));
            }
            return;
        }
        case OpSoftEnv:
            return;
        default:
            break;
    }
    warn(e, "opcode " + std::to_string(e.op) + " has no rhythm MML and is left out");
    if (e.len >= 0) rest(e, e.len);
}

// A count of one tick has no way to be written. It is written as two, and the
// tick is taken back from the next event long enough to give it.
int TrackWriter::settle(const Event& e, int ticks) {
    if (debt_ > 0 && ticks - debt_ >= 2) {
        ticks -= debt_;
        debt_ = 0;
    }
    if (ticks == 1) {
        warn(e, "one tick cannot be written; it is two, and the tick comes off what follows");
        debt_ += 1;
        ticks = 2;
    }
    return ticks;
}

std::string TrackWriter::noteToken(const std::string& pitch, int noteNumber, int ticks) {
    const Lengths& L = lengths();
    if (noteNumber < 0) return pitch + L.text(ticks);
    // N takes its length from L alone: a number written after N would run on
    // into the note number. Length 0 has no L, so it goes through a macro.
    if (ticks == 0) {
        shared_.zeroMacro = true;
        return "n" + std::to_string(noteNumber) + "=Z;";
    }
    std::string t;
    if (lengthL_ != ticks) {
        t = "l" + L.text(ticks);
        lengthL_ = ticks;
    }
    return t + "n" + std::to_string(noteNumber);
}

void TrackWriter::note(std::size_t i, const std::string& pitch, int noteNumber) {
    const Event& e = events_[i];
    if (dialect_ == Dialect::Adpcm && !voiced_) shared_.filesSounded.insert(0);
    voiced_ = true;
    const int ticks = settle(e, e.len);
    const Lengths& L = lengths();

    if (ticks <= kPieceMax && L.has(ticks)) {
        put(noteToken(pitch, noteNumber, ticks));
        portaPending_ = false;
        return;
    }

    // A note whose next event carries it on sounds to its end whatever Q says.
    // So does every note under Q8.
    bool joined = false;
    if (i + 1 < events_.size()) {
        const Event& n = events_[i + 1];
        joined = n.op == OpTie || (n.op == OpPorta && n.word(0) == kPortaFromHere);
    }
    if (portaPending_) {
        warn(e, "a portamento now closes over the first of the tied notes this note becomes");
    }
    portaPending_ = false;

    auto chain = [&](int total) {
        std::vector<int> ps = L.pieces(total);
        for (std::size_t k = 0; k < ps.size(); ++k) {
            if (k) put("&");
            put(noteToken(pitch, noteNumber, ps[k]));
        }
    };
    if (joined || quant_ == 8) {
        chain(ticks);
        return;
    }

    // Under Qn only the first n/8 sounds. Tied notes key off only in the last
    // of them, so the sounding part is written under Q8 and the rest as rests.
    if (qUncertain_) {
        warn(e, "Q may differ here depending on the way the player came; Q" +
                    std::to_string(quant_) + " is assumed");
    }
    const int sounding = std::max(1, (ticks * quant_) >> 3);
    put("q8");
    chain(sounding);
    for (int p : L.pieces(ticks - sounding)) put("r" + L.text(p));
    put("q" + std::to_string(quant_));
}

void TrackWriter::rest(const Event& e, int ticks) {
    portaPending_ = false;
    ticks = settle(e, ticks);
    for (int p : lengths().pieces(ticks)) put("r" + lengths().text(p));
}

void TrackWriter::wait(const Event& e, int ticks) {
    if (ticks == 0) return;  // waits for nothing
    ticks = settle(e, ticks);
    for (int p : lengths().pieces(ticks)) put("@w" + lengths().text(p));
}

void TrackWriter::hit(const Event& e, int ticks) {
    const std::uint8_t bits = e.arg[0] & 0x1F;
    if (bits == 0) {
        // Striking nothing still clears the rhythm keys, which a rest does not.
        warn(e, "a rhythm hit with no instrument has no MML; a rest takes its time");
        rest(e, ticks);
        return;
    }
    static const struct {
        std::uint8_t bit;
        char letter;
    } kInstruments[] = {{kRhythmBass, 'b'}, {kRhythmSnare, 's'}, {kRhythmTom, 'm'},
                        {kRhythmCymbal, 'c'}, {kRhythmHiHat, 'h'}};
    std::string t;
    for (const auto& k : kInstruments) {
        if (!(bits & k.bit)) continue;
        t.push_back(k.letter);
        if (accent_ & k.bit) t.push_back('!');
    }
    if (accent_ & ~bits & 0x1F) ++accentsDropped_;
    ticks = settle(e, ticks);
    std::vector<int> ps = lengths().pieces(ticks);
    put(t + lengths().text(ps[0]));
    // The rest of a long strike is waited out: @W touches no key, as C8's own
    // length does not.
    for (std::size_t k = 1; k < ps.size(); ++k) put("@w" + lengths().text(ps[k]));
}

// 82: a number the chip resolves by itself.
void TrackWriter::voice(const Event& e) {
    const int n = e.arg[0];
    voiced_ = true;
    if (dialect_ == Dialect::Adpcm) {
        if (n >= kPcmVoiceMax) {
            warn(e, "voice file " + std::to_string(n) + " is past 63 and is left out");
            return;
        }
        shared_.filesSounded.insert(n);
        put("@" + std::to_string(n));
        return;
    }
    if (family_ == Family::Fm) {
        // OPLLEX reads bank and preset from bits 5-4 and 3-0, and OPL2EX reads
        // nothing. Preset 0 of a bank is the chip's user voice, which @n cannot
        // name.
        const int preset = n & 0x3F;
        if (preset == 0) {
            warn(e, "FM voice number " + std::to_string(n) +
                        " names the user voice, which @n cannot, and is left out");
            return;
        }
        put("@" + std::to_string(kOpllBanked + preset));
        return;
    }
    if (chunk_.device == DevSCC && n >= kPresetWaveCount && n < kUserWaveBase + kUserWaveCount) {
        warn(e, "SCC wave " + std::to_string(n) + " has no record in the block and is left out");
        return;
    }
    put("@" + std::to_string(n));
}

// 85: a slot of the voice set. Its record becomes a #voice or #wave, numbered
// in the order the tracks first name them - the order the compiler fills the
// set in, so the numbers come out the same the next time round.
void TrackWriter::seqVoice(const Event& e) {
    const int slot = e.arg[0];
    voiced_ = true;
    if (slot >= kVoiceSlots || !shared_.slots[static_cast<std::size_t>(slot)].present) {
        warn(e, "voice slot " + std::to_string(slot) + " has no record in the block and is left out");
        return;
    }
    const Slot& held = shared_.slots[static_cast<std::size_t>(slot)];
    const VoiceRecord& record = held.record;

    const bool wantsWave = chunk_.device == DevSCC;
    if ((family_ == Family::Fm && dialect_ != Dialect::Adpcm) || wantsWave) {
        if (held.isWave != wantsWave) {
            warn(e, "voice slot " + std::to_string(slot) + " holds " +
                        (held.isWave ? "a waveform" : "an FM voice") +
                        ", which this channel cannot play, and is left out");
            return;
        }
    }

    if (family_ == Family::Fm && dialect_ != Dialect::Adpcm) {
        auto it = shared_.voiceOfSlot.find(slot);
        if (it == shared_.voiceOfSlot.end()) {
            int n = kUserVoiceBase + static_cast<int>(shared_.voiceDefs.size());
            it = shared_.voiceOfSlot.emplace(slot, n).first;
            shared_.voiceDefs.push_back({n, record});
        }
        put("@" + std::to_string(it->second));
        return;
    }
    if (chunk_.device == DevSCC) {
        auto it = shared_.waveOfSlot.find(slot);
        if (it == shared_.waveOfSlot.end()) {
            int n = -1;
            if (static_cast<int>(shared_.waveDefs.size()) < kUserWaveCount) {
                n = kUserWaveBase + static_cast<int>(shared_.waveDefs.size());
                shared_.waveDefs.push_back({n, record});
            } else {
                for (int p = 0; p < kPresetWaveCount; ++p) {
                    if (presetWave(p) == record) {
                        n = p;
                        break;
                    }
                }
            }
            if (n < 0) {
                warn(e, "more waves than #wave has numbers for; this one is left out");
                return;
            }
            it = shared_.waveOfSlot.emplace(slot, n).first;
        }
        put("@" + std::to_string(it->second));
        return;
    }
    warn(e, "a voice slot on a channel that takes no record is left out");
}

// ---------------------------------------------------------------------------
// the text
// ---------------------------------------------------------------------------

std::string hexByte(std::uint8_t b) {
    static const char* digits = "0123456789ABCDEF";
    return std::string("$") + digits[b >> 4] + digits[b & 0x0F];
}

// Written packed, as the chunk has it, so every bit comes back. A row for the
// FB/CNT and transpose bytes, then one for each operator.
void writeVoice(std::string& out, int number, const VoiceRecord& r) {
    const std::string head = std::string("#voice ") + voiceFormatSymbol(VoiceFormat::Opl) + " @" +
                             std::to_string(number) + " ";
    const std::string indent(head.size(), ' ');
    out += head;
    static const std::size_t kRows[] = {2, 5, 5};
    std::size_t at = 0;
    for (std::size_t row : kRows) {
        if (at) out += ", \\\n" + indent;
        for (std::size_t i = at; i < at + row; ++i) out += (i > at ? "," : "") + hexByte(r[i]);
        at += row;
    }
    out += "\n";
}

// MUSICA when all three rates are among its 33, which reads as ENV COPY's
// numbers; RAW otherwise, so that no byte has to move.
void writeEnv(std::string& out, int number, const EnvRecord& v) {
    int rates[3];
    const bool musica = envRateOf(v[0], rates[0]) && envRateOf(v[1], rates[1]) &&
                        envRateOf(v[3], rates[2]);
    out += std::string("#env ") + envFormatSymbol(musica ? EnvFormat::Musica : EnvFormat::Raw) +
           " @E" + std::to_string(number) + " ";
    if (musica) {
        out += std::to_string(rates[0]) + "," + std::to_string(rates[1]) + "," +
               std::to_string(v[2]) + "," + std::to_string(rates[2]);
    } else {
        out += hexByte(v[0]) + "," + hexByte(v[1]) + "," + std::to_string(v[2]) + "," + hexByte(v[3]);
    }
    out += "\n";
}

void writeWave(std::string& out, int number, const VoiceRecord& r) {
    const std::string head = "#wave " + std::to_string(number) + " ";
    const std::string indent(head.size(), ' ');
    out += head;
    for (std::size_t i = 0; i < r.size(); ++i) {
        if (i && i % 8 == 0) out += ", \\\n" + indent;
        else if (i) out += ",";
        std::string v = std::to_string(static_cast<int>(static_cast<std::int8_t>(r[i])));
        out += std::string(v.size() < 4 ? 4 - v.size() : 0, ' ') + v;
    }
    out += "\n";
}

void writeTrack(std::string& out, int index, const std::vector<std::string>& tokens) {
    const std::string head = std::string(1, static_cast<char>('A' + index)) + "  ";
    std::string line = head;
    // The reader skips spaces wherever they fall, so they are there for the eye.
    for (const std::string& t : tokens) {
        if (line.size() > head.size() && line.size() + 1 + t.size() > kLineWidth) {
            out += line + "\n";
            line = head;
        } else if (line.size() > head.size()) {
            line += " ";
        }
        line += t;
    }
    if (line.size() > head.size()) out += line + "\n";
}

bool hasSpace(const std::string& s) {
    return s.find_first_of(" \t") != std::string::npos;
}

} // namespace

bool decompileBlock(const std::vector<std::uint8_t>& b, const DecompileOptions& opt,
                    std::string& mml, Diagnostics& diag) {
    auto fail = [&](const std::string& message) {
        diag.error(opt.name, 0, 0, message);
        return false;
    };
    auto word = [&](std::size_t at) { return static_cast<unsigned>(b[at] | (b[at + 1] << 8)); };

    if (b.size() < 7 || b[0] != 'Y' || b[1] != '8' || b[2] != 'S' || b[3] != 'Q') {
        return fail("this is not a Y8SQ block");
    }
    if (b[4] != 1) {
        return fail("this block is version " + std::to_string(b[4]) + "; version 1 is the one read here");
    }
    const std::size_t size = word(5);
    if (size < 7 || size > b.size()) return fail("the block's header gives a size the file does not have");

    std::array<TrackChunk, kTrackCount> tracks;
    Shared shared;
    std::vector<VoiceFile> chunkFiles;

    for (std::size_t at = 7; at < size;) {
        if (at + 3 > size) return fail("a chunk runs past the end of the block");
        const std::uint8_t type = b[at];
        const std::size_t len = word(at + 1);
        const std::size_t body = at + 3;
        if (body + len > size) return fail("a chunk runs past the end of the block");
        at = body + len;

        if (type == kChunkTrack) {
            if (len < 4) return fail("a track chunk is too short to hold its track");
            const int index = b[body];
            const int device = b[body + 1];
            const int channel = b[body + 2];
            if (index >= kTrackCount) return fail("a track chunk names a track that does not exist");
            if (device >= kDeviceCount) {
                // Y8SQ also carries chips no Y8960 has. Their tracks are left
                // out and the rest still plays, as the ROM's reader does.
                diag.warning(opt.name, 0, 0,
                             std::string("track ") + static_cast<char>('A' + index) + " is on device " +
                                 std::to_string(device) + ", which this MML cannot write; it is left out");
                continue;
            }
            if (!deviceHasChannel(static_cast<Device>(device), channel)) {
                return fail("a track chunk names a channel its device does not have");
            }
            TrackChunk& t = tracks[static_cast<std::size_t>(index)];
            t.present = true;
            t.device = static_cast<Device>(device);
            t.channel = channel;
            t.bytes.assign(b.begin() + static_cast<std::ptrdiff_t>(body + 3),
                           b.begin() + static_cast<std::ptrdiff_t>(body + len));
        } else if (type == kChunkVoice || type == kChunkWave) {
            const bool wave = type == kChunkWave;
            const std::size_t record = wave ? kVoiceRecordSize : kPackedVoiceSize;
            if (len != 1 + record || b[body] >= kSlotIndexLimit) {
                return fail(wave ? "a waveform chunk is not an index and 32 bytes"
                                 : "a voice chunk is not an index and 12 bytes");
            }
            if (b[body] < kVoiceSlots) {
                Slot& s = shared.slots[b[body]];
                s.present = true;
                s.isWave = wave;
                const auto from = b.begin() + static_cast<std::ptrdiff_t>(body + 1);
                s.record = VoiceRecord{};
                std::copy(from, from + static_cast<std::ptrdiff_t>(record), s.record.begin());
            }
        } else if (type == kChunkVoiceFile) {
            if (len == 7) {
                chunkFiles.push_back({b[body], static_cast<int>(word(body + 1)),
                                      static_cast<int>(word(body + 3)),
                                      static_cast<int>(word(body + 5))});
            }
        } else if (type == kChunkEnvelope) {
            if (len != 1 + kEnvValues || b[body] == 0 || b[body] > kEnvMax) {
                return fail("an envelope chunk is not a number from 1 to 31 and four values");
            }
            EnvRecord env{};
            std::copy(b.begin() + static_cast<std::ptrdiff_t>(body + 1),
                      b.begin() + static_cast<std::ptrdiff_t>(body + len), env.begin());
            // Values the format forbids have no #env to say them; the nearest
            // that may be written stands in.
            bool bent = false;
            for (int i : {0, 1, 3}) {
                std::uint8_t& r = env[static_cast<std::size_t>(i)];
                if (envRateValid(r)) continue;
                if ((r >> 4) == 0) r = static_cast<std::uint8_t>(r | 0x10);
                if ((r & 0x0F) == 0) r = static_cast<std::uint8_t>(r | 0x01);
                bent = true;
            }
            if (env[2] > kEnvLevelMax) {
                env[2] = kEnvLevelMax;
                bent = true;
            }
            if (bent) {
                diag.warning(opt.name, 0, 0,
                             "envelope " + std::to_string(b[body]) +
                                 " holds values chunk 04 may not; the nearest it may stand in");
            }
            shared.envelopes[b[body]] = env;
        } else if (type >= kChunkDeviceOwned && type < kChunkSkippable) {
            // It belongs to the device its first byte names. One this MML
            // cannot write goes with that device's tracks, left out above.
            if (len == 0 || b[body] < kDeviceCount) {
                return fail("chunk type " + std::to_string(type) +
                            " is not one this reader knows for the device it names");
            }
        } else if (type < kChunkSkippable) {
            return fail("chunk type " + std::to_string(type) + " is not one this reader knows");
        }
    }

    std::array<std::vector<std::string>, kTrackCount> tokens;
    for (int i = 0; i < kTrackCount; ++i) {
        if (!tracks[static_cast<std::size_t>(i)].present) continue;
        TrackWriter w(i, tracks[static_cast<std::size_t>(i)], shared, opt, diag);
        if (!w.run(tokens[static_cast<std::size_t>(i)])) return false;
    }

    bool needBank = !shared.filesSounded.empty();
    if (needBank) {
        if (!opt.pcm || opt.pcmBankPath.empty()) {
            return fail("ADPCM tracks sound voice files; the Y8PC that holds them is needed");
        }
        if (hasSpace(opt.pcmBankPath)) {
            return fail("#pcmbank cannot take a path with a space in it: '" + opt.pcmBankPath + "'");
        }
        bool ok = true;
        for (int n : shared.filesSounded) {
            const VoiceFile* found = nullptr;
            for (const VoiceFile& f : opt.pcm->files) {
                if (f.number == n) found = &f;
            }
            if (!found) {
                diag.error(opt.name, 0, 0,
                           "voice file " + std::to_string(n) + " is sounded but the Y8PC has no setting for it");
                ok = false;
                continue;
            }
            for (const VoiceFile& c : chunkFiles) {
                if (c.number == n && (c.startPage != found->startPage ||
                                      c.pageCount != found->pageCount ||
                                      c.sampleRate != found->sampleRate)) {
                    diag.warning(opt.name, 0, 0,
                                 "voice file " + std::to_string(n) +
                                     " is not where the block says it is in the Y8PC; the Y8PC's is taken");
                }
            }
        }
        if (!ok) return false;
    }

    std::string out;
    out += "; y8mmld\n";
    if (shared.zeroMacro) out += "\n#define Z 0\n";
    if (needBank) out += "\n#pcmbank " + opt.pcmBankPath + "\n";
    if (!shared.envUsed.empty()) {
        out += "\n";
        for (int n : shared.envUsed) writeEnv(out, n, shared.envelopes[n]);
    }
    if (!shared.voiceDefs.empty()) {
        out += "\n";
        for (const auto& d : shared.voiceDefs) writeVoice(out, d.first, d.second);
    }
    if (!shared.waveDefs.empty()) {
        out += "\n";
        for (const auto& d : shared.waveDefs) writeWave(out, d.first, d.second);
    }

    out += "\n";
    for (int i = 0; i < kTrackCount; ++i) {
        const TrackChunk& t = tracks[static_cast<std::size_t>(i)];
        if (!t.present) continue;
        out += "#assign " + std::string(1, static_cast<char>('A' + i)) + " " +
               deviceSymbol(t.device) + " " + std::to_string(t.channel) + "\n";
    }
    bool any = false;
    for (int i = 0; i < kTrackCount; ++i) {
        if (tokens[static_cast<std::size_t>(i)].empty()) continue;
        if (!any) out += "\n";
        any = true;
        writeTrack(out, i, tokens[static_cast<std::size_t>(i)]);
    }

    mml = std::move(out);
    return true;
}

} // namespace y8
