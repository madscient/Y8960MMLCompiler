// The shape of the two files: the Y8SQ block of bytecode.md and the Y8PC file
// of pcmfile.md.

#include <cstdio>
#include <fstream>
#include <string>

#include "compile.h"
#include "testutil.h"

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
    test::check(b.size() == voiceAt + 3 + 33, "the block holds one voice chunk");
    test::checkBytes("the voice chunk head", head(std::vector<std::uint8_t>(
                                                      b.begin() + static_cast<std::ptrdiff_t>(voiceAt),
                                                      b.end()),
                                                  4),
                     bytes({0x01, 0x21, 0x00, 0x00}));  // type 01, 33 bytes, slot 0
    // "Piano 2 " is preset 1 of the table the sequence carries.
    std::string name(b.begin() + static_cast<std::ptrdiff_t>(voiceAt) + 4,
                     b.begin() + static_cast<std::ptrdiff_t>(voiceAt) + 12);
    test::check(name == "Piano 2 ", "the record is the one @1 names");

    Diagnostics wdiag;
    CompileResult wout;
    test::check(compileText("t.mml", "#assign A SCC 0\nA @2C4\n", wout, wdiag),
                "an SCC track compiles");
    test::check(wout.sequence[7 + 3 + 3 + 5] == 0x02, "an SCC waveform is chunk 02");
}

void rhythmVoices() {
    Diagnostics diag;
    CompileResult out;
    test::check(compileText("t.mml", "#assign A OPL2EX1 10\nA B8\n", out, diag),
                "an OPL2EX rhythm track compiles");
    // OPL2EX has no rhythm voices of its own, so slots 32-34 come along.
    std::size_t at = 7 + 3 + 3 + 6;
    test::check(out.sequence.size() == at + 3 * (3 + 33), "three rhythm voice chunks are there");
    test::check(out.sequence[at] == 0x01 && out.sequence[at + 3] == 32,
                "the first of them is slot 32");

    Diagnostics odiag;
    CompileResult oout;
    test::check(compileText("t.mml", "#assign A OPLLEX1 10\nA B8\n", oout, odiag),
                "an OPLLEX rhythm track compiles");
    test::check(oout.sequence.size() == 7 + 3 + 3 + 6,
                "OPLLEX has its own rhythm sounds, so none are carried");
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

int main() {
    blockShape();
    emptyTrackIsWritten();
    voiceChunks();
    rhythmVoices();
    pcmFile();
    return test::report("block_test");
}
