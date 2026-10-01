// y8mmld's promise: the MML it writes sounds as the block did, and decompiling
// what that MML compiles to gives the same MML again.
//
// "Sounds as the block did" is checked with a small player below that follows
// only what the tie-and-rest rewriting can change - which pitch sounds on which
// tick, where a note is struck, where a rhythm hit falls. It reads a track as a
// straight line, so the blocks it is used on have no loops or jumps.

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "adpcm.h"
#include "compile.h"
#include "decompile.h"
#include "opcodes.h"
#include "testutil.h"

using namespace y8;
using test::bytes;

namespace fs = std::filesystem;

namespace {

std::vector<std::uint8_t> compileOk(const std::string& what, const std::string& text,
                                    const std::string& path = "t.mml",
                                    std::vector<std::uint8_t>* pcm = nullptr) {
    Diagnostics diag;
    CompileResult out;
    if (!compileText(path, text, out, diag)) {
        test::check(false, what + " compiles");
        for (const Diagnostic& d : diag.all()) std::cerr << "  " << d.format() << "\n";
        std::cerr << text;
        return {};
    }
    if (pcm) *pcm = out.pcm;
    return out.sequence;
}

std::string decompileOk(const std::string& what, const std::vector<std::uint8_t>& block,
                        bool quiet = true, const AdpcmData* pcm = nullptr,
                        const std::string& bank = std::string()) {
    Diagnostics diag;
    DecompileOptions opt;
    opt.name = what;
    opt.pcm = pcm;
    opt.pcmBankPath = bank;
    std::string mml;
    test::check(decompileBlock(block, opt, mml, diag), what + " decompiles");
    if (quiet && !diag.all().empty()) {
        test::check(false, what + " decompiles without a word");
        for (const Diagnostic& d : diag.all()) std::cerr << "  " << d.format() << "\n";
    }
    return mml;
}

struct Trip {
    std::vector<std::uint8_t> sq0, sq1, sq2;
    std::string mml1, mml2;
};

// Block -> MML -> block -> MML -> block. From the first MML on, nothing moves.
Trip trip(const std::string& what, const std::vector<std::uint8_t>& sq0, bool quiet = true) {
    Trip t;
    t.sq0 = sq0;
    t.mml1 = decompileOk(what, sq0, quiet);
    t.sq1 = compileOk(what + ", decompiled", t.mml1);
    t.mml2 = decompileOk(what + ", round two", t.sq1);
    t.sq2 = compileOk(what + ", round two", t.mml2);
    test::check(t.mml1 == t.mml2, what + ": the MML is the same the second time\n" + t.mml1 +
                                      "----\n" + t.mml2);
    test::checkBytes(what + ": the block is the same the second time", t.sq2, t.sq1);
    return t;
}

// A block with one track chunk around the given event stream.
std::vector<std::uint8_t> block(int device, int channel, const std::vector<std::uint8_t>& events) {
    std::vector<std::uint8_t> b = {'Y', '8', 'S', 'Q', 0x01, 0x00, 0x00, 0x00};
    const std::size_t body = 3 + events.size();
    b.push_back(static_cast<std::uint8_t>(body & 0xFF));
    b.push_back(static_cast<std::uint8_t>(body >> 8));
    b.push_back(0x00);
    b.push_back(static_cast<std::uint8_t>(device));
    b.push_back(static_cast<std::uint8_t>(channel));
    b.insert(b.end(), events.begin(), events.end());
    b[5] = static_cast<std::uint8_t>(b.size() & 0xFF);
    b[6] = static_cast<std::uint8_t>(b.size() >> 8);
    return b;
}

// `b` with one more chunk on the end.
std::vector<std::uint8_t> withChunk(std::vector<std::uint8_t> b, int type,
                                    const std::vector<std::uint8_t>& body) {
    b.push_back(static_cast<std::uint8_t>(type));
    b.push_back(static_cast<std::uint8_t>(body.size() & 0xFF));
    b.push_back(static_cast<std::uint8_t>(body.size() >> 8));
    b.insert(b.end(), body.begin(), body.end());
    b[5] = static_cast<std::uint8_t>(b.size() & 0xFF);
    b[6] = static_cast<std::uint8_t>(b.size() >> 8);
    return b;
}

// The event stream of track `index`.
std::vector<std::uint8_t> trackOf(const std::vector<std::uint8_t>& b, int index) {
    std::size_t at = 7;
    while (at + 3 <= b.size()) {
        std::size_t len = b[at + 1] | (b[at + 2] << 8);
        if (b[at] == 0x00 && b[at + 3] == index) {
            return std::vector<std::uint8_t>(b.begin() + static_cast<std::ptrdiff_t>(at + 6),
                                             b.begin() + static_cast<std::ptrdiff_t>(at + 3 + len));
        }
        at += 3 + len;
    }
    return {};
}

struct Heard {
    std::vector<int> pitch;       // per tick, -1 for silence
    std::vector<bool> struck;     // per tick, a key on
    std::map<int, int> hits;      // tick -> instruments | accents << 5
    bool operator==(const Heard& o) const {
        return pitch == o.pitch && struck == o.struck && hits == o.hits;
    }
};

Heard hear(const std::vector<std::uint8_t>& ev) {
    Heard h;
    int octave = 4, quant = 8, accent = 0;
    int sounding = -1;
    long offAt = -1;  // -1: until something stops it
    std::size_t i = 0;
    int prevOp = -1;
    auto length = [&]() {
        int first = ev[i++];
        return (first & 0x80) ? (((first & 0x7F) << 8) | ev[i++]) : first;
    };
    auto run = [&](int ticks) {
        for (int k = 0; k < ticks; ++k) {
            const long tick = static_cast<long>(h.pitch.size());
            if (offAt >= 0 && tick >= offAt) sounding = -1;
            h.pitch.push_back(sounding);
            h.struck.push_back(false);
        }
    };
    auto joinsNext = [&](std::size_t at) {
        return at < ev.size() &&
               (ev[at] == OpTie || (ev[at] == OpPorta && at + 2 < ev.size() && ev[at + 2] == 0x80));
    };
    while (i < ev.size() && ev[i] != OpEnd) {
        const int op = ev[i++];
        int pitch = -2;
        if (op < 12) pitch = octave * 12 + op;
        else if (op == OpNoteDown) pitch = octave * 12 - 1;
        else if (op == OpNoteUp) pitch = octave * 12 + 12;
        else if (op == OpNoteAbs) pitch = ev[i++];
        if (pitch != -2) {
            const int len = length();
            const long start = static_cast<long>(h.pitch.size());
            const bool joined = prevOp == OpTie;
            sounding = pitch;
            offAt = (quant == 8 || joinsNext(i)) ? -1 : start + std::max(1, (len * quant) >> 3);
            run(len);
            if (!joined && start < static_cast<long>(h.struck.size())) h.struck[static_cast<std::size_t>(start)] = true;
            if (!joined && len == 0) {
                h.pitch.push_back(-3);  // a strike with no time still happened
                h.struck.push_back(true);
            }
        } else if (op == OpRest) {
            sounding = -1;
            run(length());
        } else if (op == OpWait) {
            run(length());
        } else if (op == OpRhythmHit) {
            const int bits = ev[i++];
            h.hits[static_cast<int>(h.pitch.size())] = bits | ((accent & bits) << 5);
            run(length());
        } else if (op == OpRhythmAccent) {
            accent = ev[i++];
        } else if (op == OpQuantize) {
            quant = ev[i++];
        } else if (op == OpOctave) {
            octave = ev[i++];
        } else if (op == OpOctUp) {
            ++octave;
        } else if (op == OpOctDown) {
            --octave;
        } else if (op >= 0x80 && op < 0xC0) {
            ++i;
        } else if (op >= 0xD0 && op < 0xE0) {
            i += 2;
        } else if (op >= 0xE0 && op < 0xF0) {
            i += 3;
        } else if (op >= 0xF0) {
            i += 4;
        }
        prevOp = op;
    }
    return h;
}

void sameSound(const std::string& what, const Trip& t, int track = 0) {
    Heard a = hear(trackOf(t.sq0, track));
    Heard b = hear(trackOf(t.sq1, track));
    test::check(a.pitch.size() == b.pitch.size(),
                what + ": the track lasts as long (" + std::to_string(a.pitch.size()) + " and " +
                    std::to_string(b.pitch.size()) + " ticks)");
    test::check(a == b, what + ": the track sounds the same\n" + t.mml1);
}

std::string readText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeBytes(const fs::path& p, const std::vector<std::uint8_t>& b) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

// ---------------------------------------------------------------------------

// What y8mmlc writes comes back byte for byte: every length it wrote was one a
// note can carry.
void compilerOutputComesBack() {
    const struct {
        const char* what;
        std::string text;
    } cases[] = {
        {"the demo", readText(fs::u8path(Y8MMLC_SOURCE_DIR) / "examples" / "demo.mml")},
        {"notes and the running state",
         "#assign A SSGS 0\n"
         "A t150 v12 @v100 o3 l8 c d+ e- f c- b+ > c < c o9 > c o0 < c n36 l4. n40 n127 r0 r4 c1.\n"
         "A c0 @w8 p100 @p-50 ~ e4 ~-200 g4 & g8 ~0 a8 s8 m300 i3 y7,56 y8,15,240 @18 q3 c4 q8\n"},
        {"loops, blocks and marks",
         "#assign A OPLLEX1 0\n"
         "A |: c [1 d ] [2 e ] :|2 (*)1 f (tc)3 g (ds)1 (coda) a (fine)3 |: |: c :|3 :|0\n"
         "#assign B OPLLEX1 1\nB (*) c (*) d (ds) (dc) (ds)2 e (tc) (tc)255\n"},
        {"SCC waves",
         "#wave 20 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,"
         "29,30,31,32\n#assign A SCC 0\n#assign B SCC 1\nA @3 c @20 d @3 e @40 f @g0 g @g1 a\n"
         "B @20 c @0 d\n"},
        {"FM voices on two chips",
         "#voice AUDIO @150 \"Mine    \",1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24\n"
         "#assign A OPLLEX1 0\n#assign B OPL2EX2 3\nA @70 c @3 d @150 e @127 f @80 g\n"
         "B @150 c @63 d @65 c\n"},
        {"software envelopes",
         "#env MUSICA @E2 1,2,3,4\n#env RAW @E9 $22,$1F,15,$F1\n#assign A DCSG1 0\n#assign B DCSG1 3\n"
         "#assign C OPLLEX2 0\nA @e2 c @e0 d @e9 e\nB @2 c @e9 d\nC @e2 c\n"},
        {"rhythm",
         "#assign A OPL2EX1 10\nA v12 @a9 @v64 bs!h8 h8 s!8 |: b8 [1 m!c ] [2 c!4 ] :|2 r4 b0 r8\n"
         "#assign B OPLLEX2 10\nB bsmch16 (*)2 b!s!m!c!h!4 (ds)2\n"
         "#assign C OPL2EX2 10\nC @b3 @s5 @m7 @c9 @h11 v10 @s2 b0 y14,32 @w8 h8 m0 r16 c0 @w16\n"},
        {"N with a length of 0", "#define Z 0\n#assign A SSGS 0\nA n40=Z; @w4 r4 l8 n41 n42=Z; @w8 r0\n"},
        {"a track with nothing in it", "#assign C SCC 4\n#assign P DCSG2 3\nP c\n"},
    };
    for (const auto& c : cases) {
        std::vector<std::uint8_t> sq0 = compileOk(c.what, c.text);
        if (sq0.empty()) continue;
        Trip t = trip(c.what, sq0);
        test::checkBytes(std::string(c.what) + ": the first block comes back", t.sq1, t.sq0);
    }
}

// Lengths a note cannot carry come back as tied notes and rests, and sound the
// same.
void lengthsAreSplit() {
    // Q4 on 100 ticks: 50 sound, 50 do not. 256 ticks under Q4 too. Under Q8,
    // 43 ticks tied into 20. A rest of 33 and a wait of 37.
    std::vector<std::uint8_t> sq0 = block(0, 0, bytes({0x83, 0x04, 0x00, 0x64, 0x0C, 0x21,
                                                       0x02, 0x81, 0x00, 0x83, 0x08, 0x04, 0x2B,
                                                       0x45, 0x04, 0x14, 0x0E, 0x25, 0x07, 0x30,
                                                       0xFF}));
    Trip t = trip("unwritable lengths", sq0);
    sameSound("unwritable lengths", t);
    test::check(t.mml1.find("q8") != std::string::npos, "a gated note is rewritten under Q8");

    // An N whose length has no L, joined into the next by a tie.
    Trip n = trip("an N of 100 ticks", block(0, 0, bytes({0xC0, 0x30, 0x64, 0x45, 0xC0, 0x32,
                                                          0x81, 0x00, 0xFF})));
    sameSound("an N of 100 ticks", n);

    // 8191 ticks, the most Y8960SeqConverter puts in one event, under Q7.
    Trip longest = trip("8191 ticks", block(5, 0, bytes({0x83, 0x07, 0x09, 0x9F, 0xFF, 0xFF})));
    sameSound("8191 ticks", longest);

    // A rhythm hit of 51 ticks becomes the hit and rests; nothing else changes.
    Trip r = trip("rhythm lengths", block(1, 10, bytes({0xA8, 0x08, 0xC8, 0x18, 0x33, 0xA8,
                                                        0x00, 0xC8, 0x01, 0x29, 0x0E, 0x21,
                                                        0xFF})));
    sameSound("rhythm lengths", r);

    // One D8 that sets several instruments comes back as one @ for each.
    Trip levels = trip("a level for three instruments",
                       block(1, 10, bytes({0xD8, 0x15, 0x05, 0xC8, 0x10, 0x30, 0xFF})));
    test::check(levels.mml1.find("@b5 @m5 @h5") != std::string::npos,
                "D8 with three bits comes back as @b @m @h\n" + levels.mml1);
}

// One tick has no way to be written: it becomes two, taken back from what
// follows, with a warning. The track keeps its length.
void oneTick() {
    std::vector<std::uint8_t> sq0 = block(0, 0, bytes({0x00, 0x01, 0x02, 0x30, 0xFF}));
    Trip t = trip("a note of one tick", sq0, false);
    test::check(hear(trackOf(t.sq0, 0)).pitch.size() == hear(trackOf(t.sq1, 0)).pitch.size(),
                "a one tick note keeps the track's length");
}

// Blocks no compiler of this MML writes: what cannot be said is left out with
// a warning, and the rest still compiles.
void unsayable() {
    Diagnostics diag;
    DecompileOptions opt;
    opt.name = "odd";
    std::string mml;
    // Q0, tempo 20, an unknown opcode with a length, a loop end with no start.
    // Then on a rhythm channel, a hit that strikes nothing.
    bool ok = decompileBlock(block(0, 0, bytes({0x83, 0x00, 0x84, 0x14, 0x3F, 0x30, 0xE2, 0x02,
                                                0x00, 0x00, 0x00, 0x30, 0xFF})),
                             opt, mml, diag);
    test::check(ok, "an odd block still decompiles");
    test::check(diag.all().size() == 4, "each thing left out is warned about once");
    compileOk("an odd block, decompiled", mml);

    Diagnostics rdiag;
    test::check(decompileBlock(block(1, 10, bytes({0xC8, 0x00, 0x30, 0xFF})), opt, mml, rdiag) &&
                    rdiag.all().size() == 1,
                "a hit that strikes nothing is warned about");

    // An accent on the snare while only the hi-hat is struck. The player sets
    // every instrument's level at each hit, so it matters, and '!' cannot say it.
    Diagnostics adiag;
    test::check(decompileBlock(block(1, 10, bytes({0xA8, 0x08, 0xC8, 0x01, 0x30, 0xFF})), opt, mml,
                               adiag) &&
                    adiag.all().size() == 1,
                "an accent on an instrument not struck is warned about");

    Diagnostics bad;
    test::check(!decompileBlock(bytes({'Y', '8', 'S', 'Q', 0x02, 0x07, 0x00}), opt, mml, bad),
                "a newer version is not read");
    test::check(!decompileBlock(bytes({'Y', '8', 'S', 'Q', 0x01, 0x0A, 0x00, 0x05, 0x00, 0x00}), opt,
                                mml, bad),
                "an unknown chunk below 80 is not read");
}

// Y8SQ as the ROM's commit 8fa6de3 has it: devices past 7, chunks that belong
// to a device, 82 on FM counted from 0, and chunk 01 as 12 bytes.
void formatEdges() {
    DecompileOptions opt;
    opt.name = "edges";
    std::string mml;

    // A track on OPM (device 9) and OPM's voice are left out; the SSGS track
    // still comes back.
    std::vector<std::uint8_t> foreign = withChunk(block(0, 0, bytes({0x00, 0x30, 0xFF})), 0x00,
                                                  bytes({0x01, 0x09, 0x00, 0x00, 0x30, 0xFF}));
    std::vector<std::uint8_t> opmVoice(2 + 32, 0);
    opmVoice[0] = 0x09;
    foreign = withChunk(foreign, 0x40, opmVoice);
    foreign = withChunk(foreign, 0x42, std::vector<std::uint8_t>(2 + 24, 0x08));
    {
        Diagnostics diag;
        test::check(decompileBlock(foreign, opt, mml, diag), "a block with an OPM track decompiles");
        test::check(diag.all().size() == 1, "the OPM track is warned about once");
        test::check(mml.find("#assign A SSGS 0") != std::string::npos &&
                        mml.find("#assign B") == std::string::npos,
                    "only the SSGS track is assigned\n" + mml);
    }
    {
        Diagnostics diag;
        std::vector<std::uint8_t> ours(2 + 32, 0);
        ours[0] = 0x03;
        test::check(!decompileBlock(withChunk(block(0, 0, bytes({0xFF})), 0x40, ours), opt, mml, diag),
                    "a chunk 40 on OPL2EX is not read");
        test::check(!decompileBlock(withChunk(block(0, 0, bytes({0xFF})), 0x41, {}), opt, mml, diag),
                    "an empty chunk 41 is not read");
        test::check(!decompileBlock(withChunk(block(0, 0, bytes({0xFF})), 0x01,
                                              std::vector<std::uint8_t>(1 + 32, 0)),
                                    opt, mml, diag),
                    "a chunk 01 of the old 32 bytes is not read");
    }

    // 82 on OPLLEX: bits 5-4 the bank and 3-0 the preset, 7-6 ignored. The
    // old writer's 82 41 is the same preset as 82 01.
    Trip old = trip("an old 82 41", block(1, 0, bytes({0x82, 0x41, 0x00, 0x30, 0xFF})));
    test::check(old.mml1.find("@65") != std::string::npos, "82 41 comes back as @65\n" + old.mml1);
    test::checkBytes("and compiles to 82 01", trackOf(old.sq1, 0), bytes({0x82, 0x01, 0x00, 0x30, 0xFF}));
    {
        Diagnostics diag;
        test::check(decompileBlock(block(1, 0, bytes({0x82, 0x10, 0x00, 0x30, 0xFF})), opt, mml, diag) &&
                        mml.find("@80") != std::string::npos && diag.all().empty(),
                    "82 10 is @80");
        Diagnostics user;
        test::check(decompileBlock(block(1, 0, bytes({0x82, 0x40, 0x00, 0x30, 0xFF})), opt, mml, user) &&
                        user.all().size() == 1,
                    "82 40, the user voice, is warned about");
    }

    // B1 was the SSGS pan and is gone; 87 is the pan now.
    {
        Diagnostics diag;
        test::check(decompileBlock(block(0, 0, bytes({0xB1, 0x03, 0x87, 0x05, 0x00, 0x30, 0xFF})), opt,
                                   mml, diag) &&
                        diag.all().size() == 1 && mml.find("i5") != std::string::npos &&
                        mml.find("i3") == std::string::npos,
                    "B1 is left out and 87 is I\n" + mml);
    }

    // B3 past 1 plays as 0.
    {
        Diagnostics diag;
        test::check(decompileBlock(block(7, 0, bytes({0xB3, 0x02, 0xFF})), opt, mml, diag) &&
                        diag.all().size() == 1 && mml.find("@g0") != std::string::npos,
                    "B3 02 is @G0 with a warning\n" + mml);
    }

    // A slot's chunk type has to fit the channel that names it.
    {
        Diagnostics diag;
        std::vector<std::uint8_t> wave = withChunk(block(1, 0, bytes({0x85, 0x00, 0x00, 0x30, 0xFF})),
                                                   0x02, std::vector<std::uint8_t>(1 + 32, 0));
        test::check(decompileBlock(wave, opt, mml, diag) && diag.all().size() == 1 &&
                        mml.find("#wave") == std::string::npos && mml.find("#voice") == std::string::npos,
                    "an FM track naming a waveform is warned about\n" + mml);
    }

    // #voice is written packed, so bits a BASIC record has no place for come
    // back too: WS bit2, which only OPL3 uses, and bit7-4 of FB/CNT.
    {
        std::vector<std::uint8_t> v(1 + 12, 0);
        v[1] = 0xF3;
        v[1 + 6] = 0x04;
        Trip t = trip("a voice BASIC cannot hold",
                      withChunk(block(3, 0, bytes({0x85, 0x00, 0x00, 0x30, 0xFF})), 0x01, v));
        test::checkBytes("a voice BASIC cannot hold: the first block comes back", t.sq1, t.sq0);
        test::check(t.mml1.find("#voice OPL @128 $F3,$00, \\") != std::string::npos,
                    "the voice is written packed\n" + t.mml1);
    }

    // 81 comes back as V where it is 4n + 67, and as @V otherwise.
    {
        Diagnostics diag;
        test::check(decompileBlock(block(0, 0, bytes({0x81, 0x63, 0x81, 0x64, 0xFF})), opt, mml, diag) &&
                        mml.find("v8 @v100") != std::string::npos,
                    "81 63 is V8 and 81 64 is @V100\n" + mml);
    }

    // Chunk 04: MUSICA when every rate is one of the 33, RAW otherwise.
    {
        Trip t = trip("an envelope of MUSICA rates",
                      withChunk(block(5, 0, bytes({0xB2, 0x01, 0x00, 0x30, 0xFF})), 0x04,
                                bytes({0x01, 0x11, 0x12, 0x08, 0x31})));
        test::check(t.mml1.find("#env MUSICA @E1 16,20,8,10\n") != std::string::npos,
                    "11 12 08 31 is MUSICA 16,20,8,10\n" + t.mml1);
        test::checkBytes("an envelope of MUSICA rates: the first block comes back", t.sq1, t.sq0);
        Trip r = trip("an envelope of raw rates",
                      withChunk(block(5, 0, bytes({0xB2, 0x01, 0x00, 0x30, 0xFF})), 0x04,
                                bytes({0x01, 0x22, 0x12, 0x08, 0x31})));
        test::check(r.mml1.find("#env RAW @E1 $22,$12,8,$31\n") != std::string::npos,
                    "22 is no MUSICA rate, so it is RAW\n" + r.mml1);
        test::checkBytes("an envelope of raw rates: the first block comes back", r.sq1, r.sq0);
        // A half of 0 and an SL past 15 may not be written; the nearest stands
        // in. 20 02 14 31 becomes 21 12 0F 31, which MUSICA names.
        Diagnostics diag;
        test::check(decompileBlock(withChunk(block(5, 0, bytes({0xB2, 0x01, 0x00, 0x30, 0xFF})), 0x04,
                                             bytes({0x01, 0x20, 0x02, 0x14, 0x31})),
                                   opt, mml, diag) &&
                        diag.all().size() == 1 && mml.find("#env MUSICA @E1 12,20,15,10\n") != std::string::npos,
                    "an envelope the format forbids is bent with a warning\n" + mml);
    }

    // (TC)0 and (FINE)0 may not be written, and MML cannot say them.
    {
        Diagnostics diag;
        test::check(decompileBlock(block(0, 0, bytes({0xD4, 0x00, 0x00, 0x00, 0x30, 0xFF})), opt, mml, diag) &&
                        diag.all().size() == 1 && mml.find("(fine)") == std::string::npos,
                    "a fine with a count of 0 is left out with a warning\n" + mml);
    }
}

// The Y8PC goes back into #pcmbank as it stands, so both files come back.
void adpcm() {
    const fs::path dir = fs::temp_directory_path() / "y8mmld_adpcm_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string bank =
        (fs::u8path(Y8MMLC_SOURCE_DIR) / "presets" / "wavs_y8950_adpcmb_excerpt.json").generic_u8string();
    std::vector<std::uint8_t> pc0;
    std::vector<std::uint8_t> sq0 =
        compileOk("an ADPCM track", "#pcmbank " + bank + "\n#assign A OPL2EX1 9\nA c @2 d @35 e\n",
                  "t.mml", &pc0);
    writeBytes(dir / "T.PC", pc0);

    AdpcmData pcm;
    std::string error;
    test::check(parsePcmFile(std::string(pc0.begin(), pc0.end()), pcm, error), "the Y8PC reads back");
    std::string mml = decompileOk("an ADPCM track", sq0, true, &pcm, "T.PC");
    test::check(mml.find("#pcmbank T.PC\n") != std::string::npos, "#pcmbank names the Y8PC");

    std::vector<std::uint8_t> pc1;
    std::vector<std::uint8_t> sq1 =
        compileOk("the ADPCM MML", mml, (dir / "t.mml").generic_u8string(), &pc1);
    test::checkBytes("the block comes back", sq1, sq0);
    test::check(pc1 == pc0, "the Y8PC comes back");

    Diagnostics diag;
    DecompileOptions none;
    std::string out;
    test::check(!decompileBlock(sq0, none, out, diag), "ADPCM with no Y8PC is refused");

    // A Y8PC has no names for #adpcm to name.
    Diagnostics named;
    CompileResult r;
    test::check(!compileText((dir / "u.mml").generic_u8string(),
                             "#pcmbank T.PC\n#adpcm 3 snare\n#assign A OPL2EX1 9\nA c\n", r, named),
                "#adpcm with a Y8PC is refused");
    fs::remove_all(dir);
}

} // namespace

int main() {
    compilerOutputComesBack();
    lengthsAreSplit();
    oneTick();
    unsayable();
    formatEdges();
    adpcm();
    return test::report("decompile_test");
}
