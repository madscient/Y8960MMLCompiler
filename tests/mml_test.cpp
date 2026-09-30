// The expected byte strings marked "ROM" are the ones Y8960BasicExtension's
// doc/plan.md records from its own tests on the openMSX fork. They are taken on
// an SSGS track, where nothing is written in front of the first note: the FM
// family puts the default voice there.

#include <string>

#include "compile.h"
#include "mml.h"
#include "source.h"
#include "testutil.h"

using namespace y8;
using test::bytes;

namespace {

// Compiles one track of MML on one device and hands back its event stream.
std::vector<std::uint8_t> track(const std::string& assign, const std::string& mml,
                                const std::string& extra = std::string()) {
    std::string text = extra + "#assign A " + assign + "\nA " + mml + "\n";
    Diagnostics diag;
    SourceFile src;
    if (!readSourceText("test.mml", text, src, diag)) {
        for (const Diagnostic& d : diag.all()) std::cerr << "  " << d.format() << "\n";
        return {};
    }
    Sequence seq;
    if (!compileSequence(src, seq, diag)) {
        for (const Diagnostic& d : diag.all()) std::cerr << "  " << d.format() << "\n";
        return {};
    }
    return seq.tracks[0].bytes;
}

// True when the MML is refused. Nothing is written for a refused track.
bool refused(const std::string& assign, const std::string& mml) {
    std::string text = "#assign A " + assign + "\nA " + mml + "\n";
    Diagnostics diag;
    SourceFile src;
    if (!readSourceText("test.mml", text, src, diag)) return true;
    Sequence seq;
    return !compileSequence(src, seq, diag);
}

void notesAndLengths() {
    // ROM: L8CDE
    test::checkBytes("L8CDE", track("SSGS 0", "L8CDE"),
                     bytes({0x00, 0x18, 0x02, 0x18, 0x04, 0x18, 0xFF}));
    // ROM: the same with R4. added
    test::checkBytes("L8CDER4.", track("SSGS 0", "L8CDER4."),
                     bytes({0x00, 0x18, 0x02, 0x18, 0x04, 0x18, 0x0C, 0x48, 0xFF}));
    // ROM: a whole note is 192 ticks, which is the long form
    test::checkBytes("T120V15O3Q4A1", track("SSGS 0", "T120V15O3Q4A1"),
                     bytes({0x84, 0x78, 0x81, 0x7F, 0x80, 0x03, 0x83, 0x04, 0x09, 0x80, 0xC0,
                            0xFF}));
    // ROM: {CDE}4 and {C8DE}4
    test::checkBytes("{CDE}4", track("SSGS 0", "{CDE}4"),
                     bytes({0x00, 0x10, 0x02, 0x10, 0x04, 0x10, 0xFF}));
    test::checkBytes("{C8DE}4", track("SSGS 0", "{C8DE}4"),
                     bytes({0x00, 0x18, 0x02, 0x10, 0x04, 0x10, 0xFF}));

    // ROM: c- and b+ step into the next octave for the one note only, and
    // leave the running octave where it was.
    test::checkBytes("C- and B+", track("SSGS 0", "L4C-B+"),
                     bytes({0x0F, 0x30, 0x10, 0x30, 0xFF}));
    test::checkBytes("C-C and B+B", track("SSGS 0", "L4C-CB+B"),
                     bytes({0x0F, 0x30, 0x00, 0x30, 0x10, 0x30, 0x0B, 0x30, 0xFF}));
    // The other accidentals stay in the octave.
    test::checkBytes("E+F-D-C+", track("SSGS 0", "L4E+F-D-C+"),
                     bytes({0x05, 0x30, 0x04, 0x30, 0x01, 0x30, 0x01, 0x30, 0xFF}));
    // Two dots add half and then a quarter.
    test::checkBytes("C4..", track("SSGS 0", "C4.."), bytes({0x00, 0x54, 0xFF}));
    // A length divides and truncates: 192/7 is 27.
    test::checkBytes("C7", track("SSGS 0", "C7"), bytes({0x00, 0x1B, 0xFF}));
    // Octaves are 0 to 9. At either end < and > write nothing, and are not an
    // error.
    test::checkBytes("O9 then >", track("SSGS 0", "O9>"), bytes({0x80, 0x09, 0xFF}));
    test::checkBytes("O0 then <", track("SSGS 0", "O0<"), bytes({0x80, 0x00, 0xFF}));
    test::checkBytes("< down to O0", track("SSGS 0", "<<<<<"),
                     bytes({0x41, 0x41, 0x41, 0x41, 0xFF}));
    test::checkBytes("> up to O9", track("SSGS 0", ">>>>>>"),
                     bytes({0x40, 0x40, 0x40, 0x40, 0x40, 0xFF}));
    test::check(refused("SSGS 0", "O10C"), "O10 is refused");
    // $ takes two digits so the note after it stays a note.
    test::checkBytes("Y$20,$0FCDE", track("SSGS 0", "L4Y$20,$0FCDE"),
                     bytes({0xE0, 0x20, 0x0F, 0x00, 0x00, 0x30, 0x02, 0x30, 0x04, 0x30, 0xFF}));
    // Space falls anywhere, including inside a number.
    test::checkBytes("spaces", track("SSGS 0", "L 1 6 C"), bytes({0x00, 0x0C, 0xFF}));

    // ROM: an N takes a length only through a name, since all its digits are the
    // note number. Zero lengths need time between them here too. N counts from
    // O1 C and the note number from O0 C, so the byte is n + 12.
    test::checkBytes("N60=Z;R4N62=Z;", track("SSGS 0", "N60=Z;R4N62=Z;", "#define Z 0\n"),
                     bytes({0xC0, 0x48, 0x00, 0x0C, 0x30, 0xC0, 0x4A, 0x00, 0xFF}));
    test::checkBytes("N0 is O1 C", track("SSGS 0", "N0"), bytes({0xC0, 0x0C, 0x30, 0xFF}));
    test::checkBytes("N36 is O4 C", track("SSGS 0", "N36"), bytes({0xC0, 0x30, 0x30, 0xFF}));
    test::checkBytes("N127", track("SSGS 0", "N127"), bytes({0xC0, 0x8B, 0x30, 0xFF}));
    test::check(refused("SSGS 0", "N128"), "N128 is refused");
    test::check(refused("SSGS 0", "C0N62"), "a note number straight after a zero length note");
    test::check(track("SSGS 0", "N60=Z;N62=Z;", "#define Z 0\n").empty(),
                "two zero length note numbers with no time between are refused");
    test::check(refused("SSGS 0", "C0D0"), "two key-ons with no time between are refused");
    test::check(!refused("SSGS 0", "C0@W8D0"), "a wait between them is enough");
    test::check(refused("SSGS 0", "{CDE}33"), "a tuplet dividing below two ticks is refused");
    test::check(!refused("SSGS 0", "{CDE}32"), "and 32 is the last one that fits");
    test::check(refused("SSGS 0", "L97C"), "a length over 96 is refused");
    test::check(refused("SSGS 0", "{C{D}}4"), "tuplets do not nest");
    test::check(refused("SSGS 0", "O$4"), "a single hex digit is refused");
}

void voices() {
    // An FM track names a slot of the sequence's own set, and a note before the
    // first @n takes voice 0 the same way.
    test::checkBytes("FM default voice", track("OPL2EX1 0", "C4"),
                     bytes({0x85, 0x00, 0x00, 0x30, 0xFF}));
    test::checkBytes("FM @1", track("OPL2EX1 0", "@1C4"),
                     bytes({0x85, 0x00, 0x00, 0x30, 0xFF}));
    // The same number twice takes one slot; two numbers take two.
    test::checkBytes("FM @1 then @2", track("OPL2EX1 0", "@1C4@2C4@1C4"),
                     bytes({0x85, 0x00, 0x00, 0x30, 0x85, 0x01, 0x00, 0x30, 0x85, 0x00, 0x00,
                            0x30, 0xFF}));
    // OPLLEX's own presets are a number the chip resolves: @n less 64, bank in
    // bits 5-4 and preset in bits 3-0. ROM: voicenum's @65 is 82 01.
    test::checkBytes("OPLLEX @65", track("OPLLEX1 0", "@65C4"),
                     bytes({0x82, 0x01, 0x00, 0x30, 0xFF}));
    test::checkBytes("OPLLEX @80", track("OPLLEX1 0", "@80C4"),
                     bytes({0x82, 0x10, 0x00, 0x30, 0xFF}));
    test::checkBytes("OPLLEX @127", track("OPLLEX1 0", "@127C4"),
                     bytes({0x82, 0x3F, 0x00, 0x30, 0xFF}));
    // OPL2EX's driver drops it, but the number is written the same way.
    test::checkBytes("OPL2EX @65", track("OPL2EX1 0", "@65C4"),
                     bytes({0x82, 0x01, 0x00, 0x30, 0xFF}));
    // The SCC's preset waveforms are records too.
    test::checkBytes("SCC @3", track("SCC 0", "@3C4"),
                     bytes({0x85, 0x00, 0x00, 0x30, 0xFF}));
    // The SSGS packs its mixer switches into the number, and nothing looks it up.
    test::checkBytes("SSGS @96", track("SSGS 0", "@96C4"),
                     bytes({0x82, 0x60, 0x00, 0x30, 0xFF}));

    test::check(refused("OPL2EX1 0", "@64C4"), "@64 names no voice");
}

void registerRanges() {
    // Each device takes its own chip's register numbers.
    struct Case {
        const char* assign;
        const char* mml;
        bool ok;
    };
    const Case cases[] = {
        {"SSGS 0", "Y$0D,0", true},      {"SSGS 0", "Y$0E,0", false},
        {"SSGS 0", "Y$1F,0", false},     {"SSGS 0", "Y$20,0", true},
        {"SSGS 0", "Y$2D,0", true},      {"SSGS 0", "Y$2E,0", false},
        {"OPLLEX1 0", "Y$07,0", true},   {"OPLLEX1 0", "Y$08,0", false},
        {"OPLLEX1 0", "Y$0E,0", true},   {"OPLLEX1 0", "Y$18,0", true},
        {"OPLLEX1 0", "Y$19,0", false},  {"OPLLEX1 0", "Y$28,0", true},
        {"OPLLEX1 0", "Y$29,0", false},  {"OPLLEX1 0", "Y$38,0", true},
        {"OPLLEX1 0", "Y$39,0", false},  {"OPLLEX1 0", "Y$40,0", true},
        {"OPLLEX1 0", "Y$48,0", true},   {"OPLLEX1 0", "Y$49,0", false},
        {"OPL2EX1 0", "Y$03,0", true},   {"OPL2EX1 0", "Y$04,0", false},
        {"OPL2EX1 0", "Y$05,0", true},   {"OPL2EX1 0", "Y$FF,0", true},
        {"DCSG1 0", "Y7,15", true},      {"DCSG1 0", "Y8,0", false},
        {"DCSG1 0", "Y7,16", false},     {"SCC 0", "Y$FF,$FF", true},
        // A rhythm or ADPCM track writes the registers of the block it is on.
        {"OPLLEX1 10", "Y$0E,$20", true}, {"OPL2EX1 9", "Y$04,0", false},
    };
    for (const Case& c : cases) {
        test::check(refused(c.assign, c.mml) != c.ok,
                    std::string(c.assign) + " " + c.mml + (c.ok ? " is taken" : " is refused"));
    }
}

void loops() {
    // The distance is counted from just past the two bytes that hold it.
    test::checkBytes("|:C:|2", track("SSGS 0", "L8|:C:|2"),
                     bytes({0x42, 0x00, 0x18, 0xE2, 0x02, 0xFA, 0xFF, 0xFF}));

    // Each block says how far the next one is; the last one says zero, and every
    // block end jumps to where the group finished.
    test::checkBytes("blocks", track("SSGS 0", "L8|:A[1C[2D:|2"),
                     bytes({0x42, 0x09, 0x18,               // |: a
                            0xE1, 0x01, 0x05, 0x00,         // [1, next block is 5 on
                            0x00, 0x18,                     // c
                            0xD3, 0x09, 0x00,               // ] , group end is 9 on
                            0xE1, 0x02, 0x00, 0x00,         // [2, the last of the group
                            0x02, 0x18,                     // d
                            0xD3, 0x00, 0x00,               // ] , the group ends here
                            0xE2, 0x02, 0xE8, 0xFF,         // :|2, back to just past 42
                            0xFF}));

    // A ']' leaves the group open; the command after it is what closes it.
    test::checkBytes("blocks with ]", track("SSGS 0", "L8|:[1C][2D]E:|2"),
                     bytes({0x42,
                            0xE1, 0x01, 0x05, 0x00,
                            0x00, 0x18,
                            0xD3, 0x09, 0x00,
                            0xE1, 0x02, 0x00, 0x00,
                            0x02, 0x18,
                            0xD3, 0x00, 0x00,
                            0x04, 0x18,                     // e, outside the group
                            0xE2, 0x02, 0xE8, 0xFF,
                            0xFF}));

    test::check(refused("SSGS 0", "|:C"), "an open |: at the end is refused");
    test::check(refused("SSGS 0", "C:|2"), "a :| with no |: is refused");
    test::check(refused("SSGS 0", "|:A[1B|:C:|2:|2"), "a |: inside a block group is refused");
    test::check(!refused("SSGS 0", "|:A[1B]C|:D:|2:|2"), "but after the group it is fine");
    test::check(refused("SSGS 0", "|:|:|:|:|:C:|:|:|:|:|"), "five deep is refused");
}

void marks() {
    // A segno is a mark, 86 and its number, that nothing plays. The dal segno
    // carries the way back to just past it.
    test::checkBytes("segno", track("SSGS 0", "L8(*)C(DS)"),
                     bytes({0x86, 0x00, 0x00, 0x18, 0xD5, 0xFB, 0xFF, 0xFF}));
    // Of two segnos with the number, the dal segno takes the last before it.
    test::checkBytes("the last segno", track("SSGS 0", "L8(*)C(*)D(DS)"),
                     bytes({0x86, 0x00, 0x00, 0x18, 0x86, 0x00, 0x02, 0x18,
                            0xD5, 0xFB, 0xFF, 0xFF}));
    // The number picks which one.
    test::checkBytes("segno by number", track("SSGS 0", "(*)1C(*)2D(DS)1"),
                     bytes({0x86, 0x01, 0x00, 0x30, 0x86, 0x02, 0x02, 0x30,
                            0xD5, 0xF7, 0xFF, 0xFF}));
    // A dal segno with no segno behind it is a distance of zero, which is ignored.
    test::checkBytes("dal segno with no segno", track("SSGS 0", "(DS)1"),
                     bytes({0xD5, 0x00, 0x00, 0xFF}));
    // (TC) is filled in when the coda arrives; both share the track's counters.
    test::checkBytes("to coda", track("SSGS 0", "L8(TC)C(CODA)D"),
                     bytes({0xF0, 0x00, 0x02, 0x03, 0x00,   // (TC), the coda is 3 on
                            0x00, 0x18,                     // c
                            0x44,                           // (CODA)
                            0x02, 0x18,                     // d
                            0xFF}));
    // A (TC) with no coda anywhere is ignored.
    test::checkBytes("to coda with no coda", track("SSGS 0", "(TC)3"),
                     bytes({0xF0, 0x00, 0x03, 0x00, 0x00, 0xFF}));
    test::checkBytes("fine and da capo", track("SSGS 0", "(FINE)(DC)"),
                     bytes({0xD4, 0x00, 0x02, 0x43, 0xFF}));
    // Case and spaces inside the brackets do not matter.
    test::checkBytes("( d c )", track("SSGS 0", "( d c )"), bytes({0x43, 0xFF}));

    test::check(refused("SSGS 0", "(CODA)(CODA)"), "a track has one coda");
    test::check(refused("SSGS 0", "(*)4"), "a segno number over 3 is refused");
    test::check(refused("SSGS 0", "(TC)(TC)(TC)(TC)(TC)(TC)(TC)(TC)(TC)"),
                "nine marks in a track are refused");
}

void rhythmTrack() {
    // The accent set is written only when it moves, and a run of letters sounds
    // together.
    test::checkBytes("rhythm", track("OPL2EX1 10", "V10@A3BSH8H8S!H8"),
                     bytes({0xA9, 0x0A,                     // v10
                            0xAA, 0x03,                     // @a3
                            0xA8, 0x00,                     // no accent yet
                            0xC8, 0x19, 0x18,               // bass+snare+hihat, an eighth
                            0xC8, 0x01, 0x18,               // hihat
                            0xA8, 0x08,                     // the snare takes the accent
                            0xC8, 0x09, 0x18,               // snare+hihat
                            0xFF}));
    test::check(refused("OPL2EX1 10", "A4"), "a letter naming no instrument is refused");
    test::check(refused("OPL2EX1 10", "L8B8"), "a rhythm track has no L");
    test::check(refused("SSGS 0", "B!8"), "'!' means nothing off a rhythm track");

    // ROM: a strike of length 0, then @W1 - a whole note in the long form.
    test::checkBytes("T60B0@W1H1", track("OPLLEX1 10", "T60B0@W1H1"),
                     bytes({0x84, 0x3C, 0xA8, 0x00, 0xC8, 0x10, 0x00, 0x0E, 0x80, 0xC0, 0xC8,
                            0x01, 0x80, 0xC0, 0xFF}));
    // ROM: two strikes on one tick are refused, and R0 and @W0 do not part them.
    test::check(refused("OPLLEX1 10", "B0H1"), "two strikes on one tick are refused");
    test::check(refused("OPLLEX1 10", "B0R0H1"), "R0 does not part two strikes");
    test::check(refused("OPLLEX1 10", "B0@W0H1"), "@W0 is refused on a rhythm track");
    test::check(!refused("OPLLEX1 10", "B0R1H1"), "a rest with a length parts two strikes");

    // One instrument's plain level: D8, the instrument's bit, the level.
    test::checkBytes("@B3@S5@M7@C9@H11", track("OPLLEX1 10", "@B3@S5@M7@C9@H11V10@s2"),
                     bytes({0xD8, 0x10, 0x03, 0xD8, 0x08, 0x05, 0xD8, 0x04, 0x07, 0xD8, 0x02,
                            0x09, 0xD8, 0x01, 0x0B, 0xA9, 0x0A, 0xD8, 0x08, 0x02, 0xFF}));
    // ROM: past 15, no number and a letter naming no instrument are refused.
    test::check(!refused("OPLLEX1 10", "@B15@S0B4"), "@B15 and @S0 are taken");
    test::check(!refused("OPLLEX1 10", "B0@W8H8"), "@W takes time on a rhythm track");
    test::check(refused("OPLLEX1 10", "@B16B4"), "@B16 is refused");
    test::check(refused("OPLLEX1 10", "@SB4"), "@S with no number is refused");
    test::check(refused("OPLLEX1 10", "@X3B4"), "@X names no instrument");
}

void adpcmTrack() {
    // @n is a voice file number the chip resolves; no record stands behind it.
    test::checkBytes("ADPCM", track("OPL2EX1 9", "@2O5E4"),
                     bytes({0x82, 0x02, 0x80, 0x05, 0x04, 0x30, 0xFF}));
    test::check(!refused("OPL2EX1 9", "@63C4"), "63 is the last voice file number");
    test::check(refused("OPL2EX1 9", "@64C4"), "a voice file number over 63 is refused");
}

void ssgCommands() {
    // ROM: ssgenv's i3 is 87 03, the pan every device shares.
    test::checkBytes("I3", track("SSGS 0", "I3C4"), bytes({0x87, 0x03, 0x00, 0x30, 0xFF}));
    // The other drivers drop it; the ROM's compiler writes it all the same.
    test::checkBytes("I15 on FM", track("OPLLEX1 0", "I15"), bytes({0x87, 0x0F, 0xFF}));
    test::check(refused("SSGS 0", "I16C4"), "I16 is refused");
}

void volumeTable() {
    // B3 on the SCC; the SSGS, FM and rhythm read it and drop it.
    test::checkBytes("@G0 on the SCC", track("SCC 0", "@G0"), bytes({0xB3, 0x00, 0xFF}));
    test::checkBytes("@G1 on the SCC", track("SCC 0", "@G1"), bytes({0xB3, 0x01, 0xFF}));
    test::checkBytes("@G0 on the SSGS", track("SSGS 0", "@G0C4"), bytes({0x00, 0x30, 0xFF}));
    test::checkBytes("@G0 on FM", track("OPL2EX1 0", "@G0"), bytes({0xFF}));
    test::checkBytes("@G1 on rhythm", track("OPLLEX1 10", "@G1B8"),
                     bytes({0xA8, 0x00, 0xC8, 0x10, 0x18, 0xFF}));
    // ROM: sccgain's @G2 is Illegal function call. The range holds everywhere.
    test::check(refused("SCC 0", "@G2"), "@G2 is refused");
    test::check(refused("SSGS 0", "@G2"), "@G2 is refused off the SCC too");
    test::check(refused("OPLLEX1 10", "@G2B8"), "@G2 is refused on rhythm too");
    test::check(refused("SCC 0", "@GC4"), "@G with no number is refused");
    // The ROM counts a tuplet's notes by letter, so the G of @G is one: four
    // notes' worth, 12 ticks each.
    test::checkBytes("{@G0CDE}4", track("SCC 0", "{@G0CDE}4"),
                     bytes({0xB3, 0x00, 0x85, 0x00, 0x00, 0x0C, 0x02, 0x0C, 0x04, 0x0C, 0xFF}));
}

void envelopes() {
    const std::string env = "#env 1 16,20,8,10\n";
    // ROM's softenv test: @E1V15L2O4CR1 on the SSGS.
    test::checkBytes("@E1 on the SSGS", track("SSGS 0", "@E1V15L2O4CR1", env),
                     bytes({0xB2, 0x01, 0x81, 0x7F, 0x80, 0x04, 0x00, 0x60, 0x0C, 0x80, 0xC0,
                            0xFF}));
    test::checkBytes("@E1 on the SCC", track("SCC 0", "@E1C4", env),
                     bytes({0xB2, 0x01, 0x85, 0x00, 0x00, 0x30, 0xFF}));
    test::checkBytes("@E1 on the DCSG", track("DCSG1 0", "@E1C4", env),
                     bytes({0xB2, 0x01, 0x00, 0x30, 0xFF}));
    // @E0 is no envelope and needs no #env.
    test::checkBytes("@E0", track("SSGS 0", "@E0C4"), bytes({0xB2, 0x00, 0x00, 0x30, 0xFF}));
    // Space may fall between @E and its number, as anywhere else.
    test::checkBytes("@e 1", track("SSGS 0", "@e 1C4", env),
                     bytes({0xB2, 0x01, 0x00, 0x30, 0xFF}));

    // On the FM family it is read and dropped, defined or not.
    test::checkBytes("@E1 on FM", track("OPL2EX1 0", "@E1C4"),
                     bytes({0x85, 0x00, 0x00, 0x30, 0xFF}));
    test::checkBytes("@E1 on ADPCM", track("OPL2EX1 9", "@E1@2O5E4"),
                     bytes({0x82, 0x02, 0x80, 0x05, 0x04, 0x30, 0xFF}));
    test::checkBytes("@E1 on rhythm", track("OPLLEX1 10", "@E1B8"),
                     bytes({0xA8, 0x00, 0xC8, 0x10, 0x18, 0xFF}));

    test::check(refused("SSGS 0", "@E2C4"), "@E2 with no #env is refused");
    test::check(refused("SSGS 0", "@E32C4"), "@E32 is refused");
    test::check(refused("OPL2EX1 0", "@E32C4"), "@E32 is refused on FM too");
    test::check(refused("SSGS 0", "@EC4"), "@E with no number is refused");

    // #env: a number 1-31 and four values 0-255, not checked against what the
    // player takes.
    const struct {
        const char* line;
        bool ok;
    } cases[] = {
        {"#env 1 16,20,8,10", true},    {"#env 31 0, 0, 0, 0", true},
        {"#env 2 $FF,255,99,40", true}, {"#env 0 1,2,3,4", false},
        {"#env 32 1,2,3,4", false},     {"#env 1 1,2,3", false},
        {"#env 1 1,2,3,4,5", false},    {"#env 1 1,2,3,256", false},
        {"#env 1 1,2,-3,4", false},     {"#env 1 \"ab\",3,4", false},
        {"#env 1", false},
    };
    for (const auto& c : cases) {
        Diagnostics diag;
        SourceFile src;
        readSourceText("t.mml", std::string(c.line) + "\n", src, diag);
        test::check(diag.hasErrors() != c.ok,
                    std::string(c.line) + (c.ok ? " is taken" : " is refused"));
    }
    Diagnostics twice;
    SourceFile twiceSrc;
    readSourceText("t.mml", env + env, twiceSrc, twice);
    test::check(twice.hasErrors(), "the same #env number twice is refused");
}

void macros() {
    test::checkBytes("=name;", track("SSGS 0", "T=TEMPO;", "#define TEMPO 140\n"),
                     bytes({0x84, 0x8C, 0xFF}));
    test::checkBytes("Xname;", track("SSGS 0", "L8XRIFF;", "#define RIFF \"CDE\"\n"),
                     bytes({0x00, 0x18, 0x02, 0x18, 0x04, 0x18, 0xFF}));
    // Unlike the BASIC it follows, MML may carry on after the macro.
    test::checkBytes("MML after Xname;", track("SSGS 0", "L8XRIFF;F", "#define RIFF \"CDE\"\n"),
                     bytes({0x00, 0x18, 0x02, 0x18, 0x04, 0x18, 0x05, 0x18, 0xFF}));

    Diagnostics diag;
    CompileResult out;
    // A macro that names itself would never end.
    test::check(!compileText("t.mml", "#define R1 \"XR1;\"\n#assign A SSGS 0\nA XR1;\n", out, diag),
                "a macro chain that loops is refused");
}

void records() {
    // AUDIO: 32 numbers, the eight of the name included.
    const std::string voice =
        "#voice AUDIO @128 $50,$69,$61,$6E,$6F,$20,$31,$20,"
        "$00,$00,$0A,$00,$00,$00,$00,$00,"
        "$31,$0E,$D9,$11,$30,$00,$00,$00,"
        "$11,$00,$B2,$F4,$70,$00,$00,$00\n";
    test::checkBytes("@128", track("OPL2EX1 0", "@128C4", voice),
                     bytes({0x85, 0x00, 0x00, 0x30, 0xFF}));

    // The name may be written as a string, and a waveform level may be negative.
    const std::string wave =
        "#wave 16 \"unused..\", \\\n"
        "         -124,-116,-108,-100, -92, -84, -76, -68, \\\n"
        "         -60, -52, -44, -36, -28, -20, -12,  -4, \\\n"
        "            4,  12,  20,  28,  36,  44,  52,  60\n";
    test::checkBytes("@16", track("SCC 0", "@16C4", wave),
                     bytes({0x85, 0x00, 0x00, 0x30, 0xFF}));

    // An AUDIO voice is packed as it is read, the way MSAVE packs one.
    {
        Diagnostics diag;
        SourceFile src;
        test::check(readSourceText("t.mml", voice + "#assign A OPL2EX1 0\nA @128C4\n", src, diag),
                    "a #voice source reads");
        Sequence seq;
        test::check(compileSequence(src, seq, diag), "and compiles");
        test::check(seq.voices.slots().size() == 1, "one slot is taken");
        const VoiceRecord& r = seq.voices.slots()[0].record;
        test::checkBytes("the AUDIO voice, packed",
                         std::vector<std::uint8_t>(r.begin(), r.begin() + kPackedVoiceSize),
                         bytes({0x0A, 0x00, 0x0E, 0xD9, 0x11, 0x31, 0x00, 0x00, 0xB2, 0xF4, 0x11, 0x00}));
    }

    test::check(refused("OPL2EX1 0", "@128C4"), "@128 with no #voice is refused");
    test::check(refused("SCC 0", "@16C4"), "@16 with no #wave is refused");

    auto reads = [](const std::string& text) {
        Diagnostics d;
        SourceFile s;
        readSourceText("t.mml", text, s, d);
        return !d.hasErrors();
    };
    const std::string twelve = "1,2,3,4,5,6,7,8,9,10,11,12";
    const std::string twentyFour = twelve + "," + twelve;
    const std::string thirtyTwo = twentyFour + ",1,2,3,4,5,6,7,8";
    const std::string forty = thirtyTwo + ",1,2,3,4,5,6,7,8";
    const std::string fifty = forty + ",1,2,3,4,5,6,7,8,9,10";

    // The number is the user half only; the presets come from the carried table.
    test::check(!reads("#voice AUDIO @63 " + thirtyTwo + "\n"), "#voice on a preset number is refused");
    test::check(!reads("#voice OPL @64 " + twelve + "\n"), "@64 is a preset number and is refused");
    test::check(!reads("#wave 15 1,2,3\n"), "#wave on a preset number is refused");

    // Each format takes its own count, and no other.
    test::check(reads("#voice OPL @128 " + twelve + "\n"), "#voice OPL takes 12 values");
    test::check(reads("#voice Opl3 @128 " + twentyFour + "\n"), "#voice OPL3 takes 24 values");
    test::check(reads("#voice opm @128 " + thirtyTwo + "\n"), "#voice OPM takes 32 values");
    test::check(reads("#voice audio @128 " + thirtyTwo + "\n"), "#voice AUDIO takes 32 values");
    test::check(reads("#voice SFG @128 " + forty + "\n"), "#voice SFG takes 40 values");
    test::check(reads("#voice Makoto @128 " + fifty + "\n"), "#voice MAKOTO takes 50 values");
    test::check(!reads("#voice OPL @128 " + twentyFour + "\n"), "#voice OPL with 24 values is refused");
    test::check(!reads("#voice OPL3 @128 " + twelve + "\n"), "#voice OPL3 with 12 values is refused");
    test::check(!reads("#voice AUDIO @128 1,2,3\n"), "#voice AUDIO with 3 values is refused");
    test::check(!reads("#voice AUDIO @128 \"123456789\"," + twentyFour + "\n"),
                "#voice AUDIO that runs past 32 values is refused");
    test::check(!reads("#voice SFG @128 " + thirtyTwo + "\n"), "#voice SFG with 32 values is refused");
    test::check(!reads("#voice MAKOTO @128 " + forty + "\n"), "#voice MAKOTO with 40 values is refused");
    test::check(!reads("#voice OPL @128\n"), "#voice OPL with no values is refused");

    // The format comes first, and the number takes its @.
    test::check(!reads("#voice 128 " + thirtyTwo + "\n"), "#voice with no format is refused");
    test::check(!reads("#voice OPN @128 " + twelve + "\n"), "a symbol that names no format is refused");
    test::check(!reads("#voice OPL 128 " + twelve + "\n"), "#voice OPL with a number and no @ is refused");
    test::check(!reads("#voice 128 OPL " + twelve + "\n"), "the symbol after the number is refused");
    test::check(!reads("#voice OPL @ " + twelve + "\n"), "@ with no number is refused");
    test::check(!reads("#wave 16 OPL " + twelve + "\n"), "#wave takes no format");
    test::check(!reads(voice + voice), "the same number twice is refused");

    // Only voices carried as OPL can be named by a Y8960 FM track.
    test::check(!track("OPLLEX1 0", "@128C4", "#voice OPL @128 " + twelve + "\n").empty(),
                "an OPL voice plays on OPLLEX");
    test::check(track("OPL2EX1 0", "@128C4", "#voice OPL3 @128 " + twentyFour + "\n").empty(),
                "an OPL3 voice on OPL2EX is refused");
    test::check(track("OPLLEX1 0", "@128C4", "#voice OPM @128 " + thirtyTwo + "\n").empty(),
                "an OPM voice on OPLLEX is refused");
    test::check(track("OPLLEX1 0", "@128C4", "#voice SFG @128 " + forty + "\n").empty(),
                "an SFG voice on OPLLEX is refused");
}

// SFG and MAKOTO are packed into chunk 40's record as they are read. The
// expected bytes are put together by hand from the two BASICs' voice format
// tables and bytecode.md's "OPM と OPN 系の音色".
void voiceConversions() {
    auto packed = [](const std::string& text, VoiceFormat& format) {
        Diagnostics d;
        SourceFile s;
        readSourceText("t.mml", text, s, d);
        for (const Diagnostic& x : d.all()) std::cerr << "  " << x.format() << "\n";
        auto it = s.userVoices.find(128);
        if (it == s.userVoices.end()) return std::vector<std::uint8_t>();
        format = it->second.format;
        return std::vector<std::uint8_t>(it->second.record.begin(), it->second.record.end());
    };

    // SFG: LFO speed, AMD and PMD dropped; OP4-OP1 from bit6-3 to bit7-4; the
    // outputs dropped and the noise bit kept; PMS and AMS swapped into place.
    // Each operator drops velocity, level key scale and the TL offset.
    VoiceFormat f = VoiceFormat::Opl;
    std::vector<std::uint8_t> sfg = packed(
        "#voice SFG @128 $C8,$7F,$55,$7F,$FA,$5B,$9F,$F4, \\\n"
        "  $FF,$70,$AB,$F5,$FF,$FF,$FF,$3C, \\\n"
        "  $10,$00,$00,$21,$1F,$05,$40,$57, \\\n"
        "  $01,0,0,0,0,0,0,0, $02,0,0,0,0,0,0,0\n",
        f);
    test::check(f == VoiceFormat::Opm, "an SFG voice is carried as OPM");
    test::checkBytes("an SFG voice, packed", sfg,
                     bytes({0xBA, 0x35, 0xF4, 0xF0,
                            0x7F, 0xDF, 0x9F, 0xDF, 0x3C, 0x00, 0x75,
                            0x10, 0x1F, 0x05, 0x40, 0x57, 0x00, 0x21,
                            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));

    // MAKOTO: rates and levels turned round to the chip's way; SSG-EG and KS
    // out of one column; DT -3..+3 to OPN's sign bit; AMS kept, PMS and the
    // LFO dropped.
    f = VoiceFormat::Opl;
    std::vector<std::uint8_t> makoto = packed(
        "#voice MAKOTO @128 58,15,5,5,5,5,5,7,0,2, \\\n"
        "  0,3,31,0,15,127,146,5,-3,1, \\\n"
        "  31,0,0,15,0,0,0,15,3,0, \\\n"
        "  0,0,0,0,0,0,0,0,0,0, \\\n"
        "  0,0,0,0,0,100,0,0,-1,0\n",
        f);
    test::check(f == VoiceFormat::Opm, "a MAKOTO voice is carried as OPM");
    test::checkBytes("a MAKOTO voice, packed", makoto,
                     bytes({0x3A, 0x20, 0x00, 0xF0,
                            0x00, 0x9F, 0x9C, 0x00, 0x0F, 0x09, 0x75,
                            0x7F, 0x00, 0x1F, 0x1F, 0xF0, 0x00, 0x3F,
                            0x7F, 0x1F, 0x1F, 0x1F, 0xFF, 0x00, 0x00,
                            0x1B, 0x1F, 0x1F, 0x1F, 0xFF, 0x00, 0x50}));
}

void continuation() {
    Diagnostics diag;
    SourceFile src;
    // A '\' at the end of a line drops the break and joins the next one.
    const std::string text =
        "#define RIFF \"cde\"\n"
        "#assign A SSGS 0\n"
        "A L8 XRIF\\\n"
        "F; \\\n"
        "  cde\n";
    test::check(readSourceText("t.mml", text, src, diag), "a continued line reads");
    Sequence seq;
    test::check(compileSequence(src, seq, diag), "and compiles");
    test::checkBytes("a joined line", seq.tracks[0].bytes,
                     bytes({0x00, 0x18, 0x02, 0x18, 0x04, 0x18,
                            0x00, 0x18, 0x02, 0x18, 0x04, 0x18, 0xFF}));

    // The diagnostic names the physical line the error is on, not the first of
    // the joined ones.
    Diagnostics bad;
    SourceFile src2;
    readSourceText("t.mml", "#assign A SSGS 0\nA c4 \\\nd4 \\\nz4\n", src2, bad);
    Sequence seq2;
    compileSequence(src2, seq2, bad);
    test::check(bad.hasErrors() && bad.all().front().line == 4,
                "the error is reported on line 4");

    // A meta command spread over lines is one command.
    Diagnostics meta;
    SourceFile src3;
    test::check(readSourceText("t.mml", "#assign \\\n  A \\\n  SSGS \\\n  0\n", src3, meta),
                "a continued meta command reads");
    test::check(src3.tracks[0].assigned && src3.tracks[0].device == DevSSGS,
                "and does what it says");
}

void sourceLines() {
    // The lines of one track join up, and a loop may span them.
    Diagnostics diag;
    SourceFile src;
    const std::string text =
        "; a comment\n"
        "#assign A SSGS 0\n"
        "\n"
        "A L8 |: C\n"
        "A    D :|2\n";
    test::check(readSourceText("t.mml", text, src, diag), "the source reads");
    Sequence seq;
    test::check(compileSequence(src, seq, diag), "a loop may span track lines");
    test::checkBytes("joined lines", seq.tracks[0].bytes,
                     bytes({0x42, 0x00, 0x18, 0x02, 0x18, 0xE2, 0x02, 0xF8, 0xFF, 0xFF}));

    // A diagnostic points at the line the author wrote.
    Diagnostics bad;
    SourceFile src2;
    readSourceText("t.mml", "#assign A SSGS 0\nA C4\nA Z\n", src2, bad);
    Sequence seq2;
    compileSequence(src2, seq2, bad);
    test::check(bad.hasErrors() && bad.all().front().line == 3,
                "the error is reported on line 3");

    Diagnostics noAssign;
    SourceFile src3;
    readSourceText("t.mml", "A C4\n", src3, noAssign);
    Sequence seq3;
    compileSequence(src3, seq3, noAssign);
    test::check(noAssign.hasErrors(), "MML with no #assign is refused");

    Diagnostics clash;
    SourceFile src4;
    readSourceText("t.mml", "#assign A OPL2EX1 6\n#assign B OPL2EX1 10\n", src4, clash);
    test::check(clash.hasErrors(), "channel 10 and channels 6-8 cannot share a block");

    Diagnostics twice;
    SourceFile src5;
    readSourceText("t.mml", "#assign A SSGS 0\n#assign B SSGS 0\n", src5, twice);
    test::check(twice.hasErrors(), "two tracks cannot hold one channel");
}

void outputNames() {
    bool truncated = false;
    bool replaced = false;
    test::check(outputBaseName("songs/my-song.mml", truncated, replaced) == "MY_SONG" &&
                    !truncated && !replaced,
                "the base name is the stem, upper case");
    test::check(outputBaseName("averylongname.mml", truncated, replaced) == "AVERYLON" &&
                    truncated && !replaced,
                "a name over eight characters is cut and says so");

    // A character that is not ASCII is one '_' whatever its length in UTF-8.
    test::check(outputBaseName("曲1.mml", truncated, replaced) == "_1" && replaced,
                "a three byte character becomes one '_'");
    test::check(outputBaseName("テスト曲.mml", truncated, replaced) == "____" && replaced,
                "four characters become four '_'");
    test::check(outputBaseName("\xF0\x9F\x98\x80" "a.mml", truncated, replaced) == "_A" && replaced,
                "a four byte character becomes one '_'");
    // A byte that begins no well formed sequence stands for a character of its own.
    test::check(outputBaseName("\xFF" "\xE6" "b.mml", truncated, replaced) == "__B" && replaced,
                "stray bytes become one '_' each");
}

} // namespace

int main() {
    notesAndLengths();
    voices();
    registerRanges();
    loops();
    marks();
    rhythmTrack();
    adpcmTrack();
    ssgCommands();
    volumeTable();
    envelopes();
    macros();
    records();
    voiceConversions();
    continuation();
    sourceLines();
    outputNames();
    return test::report("mml_test");
}
