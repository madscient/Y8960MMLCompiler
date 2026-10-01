// The shape of the two files: the Y8SQ block of bytecode.md and the Y8PC file
// of pcmfile.md.

#include <cstdio>
#include <fstream>
#include <string>

#include "compile.h"
#include "testutil.h"
#include "voicedata.h"

using namespace y8;
using test::bytes;

namespace {

void writeText(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::vector<std::uint8_t> head(const std::vector<std::uint8_t>& v, std::size_t n) {
    return std::vector<std::uint8_t>(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n));
}

void blockShape() {
    Diagnostics diag;
    CompileResult out;
    bool ok = compileText("t.mml", "#assign A SSGS 0\nA C4\n", out, diag);
    test::check(ok, "one SSGS track compiles");
    // "Y8SQ", version 1, the whole size; then the track chunk.
    test::checkBytes("a one track block", out.sequence,
                     bytes({0x59, 0x38, 0x53, 0x51, 0x01, 0x10, 0x00,
                            0x00, 0x06, 0x00,              // a track chunk, six bytes
                            0x00, 0x00, 0x00,              // track 0, SSGS, channel 0
                            0x00, 0x30, 0xFF}));
    test::check(!out.hasPcm, "no #pcmbank means no Y8PC");
}

void emptyTrackIsWritten() {
    Diagnostics diag;
    CompileResult out;
    // A track with a channel and no MML still carries its assignment.
    test::check(compileText("t.mml", "#assign B DCSG2 3\n", out, diag), "an empty track compiles");
    test::checkBytes("an empty track", out.sequence,
                     bytes({0x59, 0x38, 0x53, 0x51, 0x01, 0x0E, 0x00,
                            0x00, 0x04, 0x00,
                            0x01, 0x06, 0x03,              // track 1, DCSG2, channel 3
                            0xFF}));                       // the terminator counts too
}

void voiceChunks() {
    Diagnostics diag;
    CompileResult out;
    test::check(compileText("t.mml", "#assign A OPL2EX1 0\nA @1C4\n", out, diag),
                "an FM track compiles");
    // The track chunk, then the record the sequence carries for @1.
    const std::vector<std::uint8_t>& b = out.sequence;
    std::size_t voiceAt = 7 + 3 + 3 + 5;  // header, chunk head, track head, events
    // "Piano 2 " is preset 1, packed: FB/CNT 08 and a transpose of 12, then
    // each operator's 40h 60h 80h 20h E0h, from BASIC's 20h 40h 60h 80h at
    // 16-19 and 24-27 and E0h at 21 and 29.
    test::checkBytes("the voice chunk",
                     std::vector<std::uint8_t>(b.begin() + static_cast<std::ptrdiff_t>(voiceAt), b.end()),
                     bytes({0x01, 0x0D, 0x00, 0x00,  // type 01, 13 bytes, slot 0
                            0x08, 0x0C,
                            0x0F, 0xD9, 0x10, 0x30, 0x00,
                            0x00, 0xB2, 0xF3, 0x10, 0x00}));

    Diagnostics wdiag;
    CompileResult wout;
    test::check(compileText("t.mml", "#assign A SCC 0\nA @2C4\n", wout, wdiag),
                "an SCC track compiles");
    test::check(wout.sequence[7 + 3 + 3 + 5] == 0x02, "an SCC waveform is chunk 02");
    test::check(wout.sequence.size() == 7 + 3 + 3 + 5 + 3 + 33, "and keeps its 32 bytes");
}

// The ROM packs a record the way VOIPACK does. Its rhythm voices it packed by
// hand from the BASIC records it used to carry, so packing those BASIC records
// here has to land on the ROM's bytes.
void packing() {
    const VoiceRecord before[kRhythmVoiceCount] = {
        {'B', 'a', 's', 's', 'D', 'r', 'u', 'm', 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x01, 0x18, 0xDF, 0x6A, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0xF8, 0x6D, 0x00, 0x00, 0x00, 0x00},
        {'H', 'i', 'H', 'a', 't', '/', 'S', 'D', 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x01, 0x00, 0xC8, 0xA7, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xD8, 0x48, 0x00, 0x00, 0x00, 0x00},
        {'T', 'o', 'm', '/', 'C', 'y', 'm', 'b', 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x05, 0x00, 0xF8, 0x59, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xAA, 0x55, 0x00, 0x00, 0x00, 0x00},
    };
    for (int i = 0; i < kRhythmVoiceCount; ++i) {
        PackedVoice p = packVoice(before[i]);
        PackedVoice rom = rhythmVoice(i);
        test::checkBytes("rhythm voice " + std::to_string(i) + " packs to the ROM's",
                         std::vector<std::uint8_t>(p.begin(), p.end()),
                         std::vector<std::uint8_t>(rom.begin(), rom.end()));
    }

    // What VOIPACK drops: MSX-AUDIO's flags in bit7-4 of byte 10, the waveform
    // bits past 1-0, and the transpose's fraction, rounded at 80h.
    VoiceRecord r{};
    r[10] = 0xF7;
    r[21] = 0xFE;
    r[29] = 0x07;
    r[8] = 0x80;
    r[9] = 0x02;
    PackedVoice p = packVoice(r);
    test::check(p[0] == 0x07 && p[6] == 0x02 && p[11] == 0x03, "the bits past the chip's are dropped");
    test::check(p[1] == 0x03, "a fraction of 80h rounds up");
    r[8] = 0x7F;
    test::check(packVoice(r)[1] == 0x02, "a fraction under 80h rounds down");
    r[8] = 0x80;
    r[9] = 0xFF;
    test::check(packVoice(r)[1] == 0x00, "-1 and a half rounds to 0");

    // An OPL #voice is packed already and goes in as written, bits BASIC has no
    // place for included (WS bit2, which OPL3 reads).
    Diagnostics diag;
    CompileResult out;
    test::check(compileText("t.mml",
                            "#voice opl @130 $8E,$FE, $18,$DF,$6A,$01,$04, $00,$F8,$6D,$01,$07\n"
                            "#assign A OPLLEX1 0\n#assign B OPL2EX2 0\nA @130C4\nB @130C4\n",
                            out, diag),
                "an OPL #voice on both chips compiles");
    const std::vector<std::uint8_t>& b = out.sequence;
    test::checkBytes("the OPL #voice's chunk",
                     std::vector<std::uint8_t>(b.end() - 16, b.end()),
                     bytes({0x01, 0x0D, 0x00, 0x00, 0x8E, 0xFE, 0x18, 0xDF, 0x6A, 0x01, 0x04, 0x00,
                            0xF8, 0x6D, 0x01, 0x07}));
    test::check(b.size() == 7 + 2 * (3 + 3 + 5) + 16, "both tracks name the one slot");
}

void rhythmVoices() {
    Diagnostics diag;
    CompileResult out;
    test::check(compileText("t.mml", "#assign A OPL2EX1 10\nA B8\n", out, diag),
                "an OPL2EX rhythm track compiles");
    // OPL2EX has no rhythm voices of its own, so slots 32-34 come along.
    std::size_t at = 7 + 3 + 3 + 6;
    test::check(out.sequence.size() == at + 3 * (3 + 13), "three rhythm voice chunks are there");
    test::checkBytes("the first of them is slot 32, packed",
                     std::vector<std::uint8_t>(out.sequence.begin() + static_cast<std::ptrdiff_t>(at),
                                               out.sequence.begin() + static_cast<std::ptrdiff_t>(at + 16)),
                     bytes({0x01, 0x0D, 0x00, 0x20, 0x0E, 0x00, 0x18, 0xDF, 0x6A, 0x01, 0x01, 0x00,
                            0xF8, 0x6D, 0x01, 0x00}));

    Diagnostics odiag;
    CompileResult oout;
    test::check(compileText("t.mml", "#assign A OPLLEX1 10\nA B8\n", oout, odiag),
                "an OPLLEX rhythm track compiles");
    test::check(oout.sequence.size() == 7 + 3 + 3 + 6,
                "OPLLEX has its own rhythm sounds, so none are carried");
}

void envelopeChunks() {
    Diagnostics diag;
    CompileResult out;
    // Only the envelopes a PSG track names are carried, and @E0 names none.
    test::check(compileText("t.mml",
                            "#env MUSICA @E1 16,20,8,10\n#env MUSICA @E2 1,2,3,4\n"
                            "#env RAW @E3 $22,$12,8,$31\n"
                            "#assign A SSGS 0\n#assign B OPL2EX1 0\n"
                            "A @E1C4@E0C4\nB @E3C4\n",
                            out, diag),
                "tracks with @E compile");
    const std::vector<std::uint8_t> tracks = {
        0x59, 0x38, 0x53, 0x51, 0x01, 0x39, 0x00,
        0x00, 0x0C, 0x00, 0x00, 0x00, 0x00,              // track 0, SSGS, channel 0
        0xB2, 0x01, 0x00, 0x30, 0xB2, 0x00, 0x00, 0x30, 0xFF,
        0x00, 0x08, 0x00, 0x01, 0x03, 0x00,              // track 1, OPL2EX1, channel 0
        0x85, 0x00, 0x00, 0x30, 0xFF};
    // Then B's default voice, then chunk 04 for envelope 1 alone.
    test::check(out.sequence.size() == tracks.size() + 3 + 13 + 8,
                "one voice chunk and one envelope chunk");
    test::checkBytes("the tracks", head(out.sequence, tracks.size()), tracks);
    // ROM: softenv's ENV COPY of AR 16 DR 20 SL 8 RR 10 is 11 12 08 31.
    test::checkBytes("chunk 04",
                     std::vector<std::uint8_t>(out.sequence.end() - 8, out.sequence.end()),
                     bytes({0x04, 0x05, 0x00, 0x01, 0x11, 0x12, 0x08, 0x31}));

    // RAW goes in as written, a byte no MUSICA rate names included.
    Diagnostics rdiag;
    CompileResult rout;
    test::check(compileText("t.mml", "#env RAW @E3 $22,$12,8,$31\n#assign A DCSG1 0\nA @E3C4\n", rout,
                            rdiag),
                "a RAW envelope compiles");
    test::checkBytes("the RAW chunk 04",
                     std::vector<std::uint8_t>(rout.sequence.end() - 8, rout.sequence.end()),
                     bytes({0x04, 0x05, 0x00, 0x03, 0x22, 0x12, 0x08, 0x31}));
}

void pcmFile() {
    const std::string json =
        "{\n"
        "  \"codec\": \"adpcm-b\",\n"
        "  \"sample_rate\": 8000,\n"
        "  \"boundary\": 256,\n"
        "  \"total_size\": 512,\n"
        "  \"entries\": [\n"
        "    { \"name\": \"bd\", \"offset\": 0, \"offset_hex\": \"0x000000\",\n"
        "      \"size\": 300, \"padded_size\": 512, \"end_hex\": \"0x0001FF\",\n"
        "      \"root_note\": 60 }\n"
        "  ]\n"
        "}\n";
    writeText("y8mmlc_test.json", json);
    {
        std::ofstream bin("y8mmlc_test.bin", std::ios::binary | std::ios::trunc);
        std::string zeros(512, '\0');
        bin.write(zeros.data(), 512);
    }
    // The bank on its own gives entry 0 the number 0; #adpcm moves it to 3.
    writeText("y8mmlc_test0.mml",
              "#pcmbank y8mmlc_test.json\n"
              "#assign A OPL2EX1 9\n"
              "A @0 O5 E4\n");
    Diagnostics bare;
    CompileResult bareOut;
    test::check(compileFile("y8mmlc_test0.mml", bareOut, bare),
                "a bank with no #adpcm is enough to sound");
    test::checkBytes("the Y8PC of a bank that numbers itself", head(bareOut.pcm, 16),
                     bytes({0x59, 0x38, 0x50, 0x43, 0x01, 0x03, 0x01, 0x02, 0x00,
                            0x00, 0x00, 0x00, 0x02, 0x00, 0x40, 0x1F}));

    writeText("y8mmlc_test.mml",
              "#pcmbank y8mmlc_test.json\n"
              "#adpcm 3 bd\n"
              "#assign A OPL2EX1 9\n"
              "A @3 O5 E4\n");

    Diagnostics diag;
    CompileResult out;
    bool ok = compileFile("y8mmlc_test.mml", out, diag);
    for (const Diagnostic& d : diag.all()) std::cerr << "  " << d.format() << "\n";
    test::check(ok, "an ADPCM track compiles");
    test::check(out.hasPcm, "#pcmbank gives a Y8PC");

    // "Y8PC", version 1, both halves present, two settings, two pages. The bank
    // gave the entry number 0 and #adpcm put the same one at 3 as well.
    test::checkBytes("the Y8PC header and settings", head(out.pcm, 23),
                     bytes({0x59, 0x38, 0x50, 0x43, 0x01, 0x03, 0x02, 0x02, 0x00,
                            0x00, 0x00, 0x00, 0x02, 0x00, 0x40, 0x1F,
                            0x03, 0x00, 0x00, 0x02, 0x00, 0x40, 0x1F}));
    test::check(out.pcm.size() == 23 + 512, "the dump follows the settings");

    // The same seven bytes are chunk 03 of the sequence.
    if (out.sequence.size() < 3 + 7) {
        test::check(false, "the block is too short to hold chunk 03");
        return;
    }
    std::size_t at = out.sequence.size() - (3 + 7);
    test::checkBytes("chunk 03", std::vector<std::uint8_t>(
                                     out.sequence.begin() + static_cast<std::ptrdiff_t>(at),
                                     out.sequence.end()),
                     bytes({0x03, 0x07, 0x00, 0x03, 0x00, 0x00, 0x02, 0x00, 0x40, 0x1F}));

    // A voice file the MML sounds and no #adpcm binds is a key-on with nothing
    // behind it.
    writeText("y8mmlc_test2.mml",
              "#pcmbank y8mmlc_test.json\n"
              "#adpcm 3 bd\n"
              "#assign A OPL2EX1 9\n"
              "A @4 O5 E4\n");
    Diagnostics unbound;
    CompileResult out2;
    test::check(!compileFile("y8mmlc_test2.mml", out2, unbound),
                "a voice file the bank has no entry for is refused");

    writeText("y8mmlc_test3.mml",
              "#pcmbank y8mmlc_test.json\n"
              "#adpcm 3 nosuch\n"
              "#assign A OPL2EX1 9\n"
              "A @3 O5 E4\n");
    Diagnostics missing;
    CompileResult out3;
    test::check(!compileFile("y8mmlc_test3.mml", out3, missing),
                "an #adpcm naming no entry is refused");

    std::remove("y8mmlc_test0.mml");
    std::remove("y8mmlc_test.json");
    std::remove("y8mmlc_test.bin");
    std::remove("y8mmlc_test.mml");
    std::remove("y8mmlc_test2.mml");
    std::remove("y8mmlc_test3.mml");
}

} // namespace

// Chunk 80 comes last, its items in number order whatever order the source
// gives them in. A source with none of the three has no chunk 80 (blockShape).
void metaChunk() {
    Diagnostics diag;
    CompileResult out;
    test::check(compileText("t.mml",
                            "#author \"C\"\n#title \"AB\"\n#pitch 442.5\n#assign A SSGS 0\nA C4\n",
                            out, diag),
                "a source with #title, #author and #pitch compiles");
    test::checkBytes("chunk 80",
                     std::vector<std::uint8_t>(out.sequence.end() - 14, out.sequence.end()),
                     bytes({0x80, 0x0B, 0x00,
                            0x01, 0x02, 0x49, 0x11,   // 4425 tenths of a hertz
                            0x03, 0x02, 'A', 'B',
                            0x04, 0x01, 'C'}));
    test::check(out.sequence.size() == 16 + 14, "the track, then chunk 80 alone");

    Diagnostics tdiag;
    CompileResult tout;
    test::check(compileText("t.mml", "#title \"X\"\n#assign A SSGS 0\nA C4\n", tout, tdiag),
                "a source with #title alone compiles");
    test::checkBytes("chunk 80 with the title alone",
                     std::vector<std::uint8_t>(tout.sequence.end() - 6, tout.sequence.end()),
                     bytes({0x80, 0x03, 0x00, 0x03, 0x01, 'X'}));
}

int main() {
    blockShape();
    emptyTrackIsWritten();
    voiceChunks();
    packing();
    rhythmVoices();
    envelopeChunks();
    metaChunk();
    pcmFile();
    return test::report("block_test");
}
