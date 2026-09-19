// Runs the y8mmlc executable itself on sources whose names and contents are
// not ASCII. What makes those work on Windows is the executable's manifest,
// which a test that calls the core library in-process would never see.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "testutil.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path u8(const std::string& s) { return fs::u8path(s); }

std::string utf8(const fs::path& p) { return p.u8string(); }

void writeFile(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

struct Run {
    int exit = -1;
    std::string out;
    std::string err;
};

// Runs y8mmlc with one argument, the way a shell would hand it over: as text,
// not as bytes in some code page.
Run run(const fs::path& dir, const std::string& arg) {
    const fs::path outFile = dir / "stdout.txt";
    const fs::path errFile = dir / "stderr.txt";
    std::string cmd = "\"" + std::string(Y8MMLC_EXE) + "\" \"" + arg + "\" > \"" + utf8(outFile) +
                      "\" 2> \"" + utf8(errFile) + "\"";
    Run r;
#ifdef _WIN32
    // cmd /c drops the first and last quote of a line that starts with one.
    cmd = "\"" + cmd + "\"";
    int n = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, wide.data(), n);
    r.exit = _wsystem(wide.c_str());
#else
    int status = std::system(cmd.c_str());
    r.exit = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    r.out = readFile(outFile);
    r.err = readFile(errFile);
    return r;
}

bool has(const std::string& text, const std::string& piece) {
    return text.find(piece) != std::string::npos;
}

} // namespace

int main() {
    const fs::path base = fs::temp_directory_path() / "y8mmlc_cli_test";
    fs::remove_all(base);
    fs::create_directories(base);

    // A #pcmbank path that is not ASCII. The file is not an adpcm_packer bank, so
    // getting as far as complaining about its codec means it was opened.
    writeFile(base / u8("音.json"), "{\"codec\":\"x\"}\n");
    writeFile(base / u8("bank.mml"), "#pcmbank 音.json\n#assign A OPL2EX1 9\nA @0 c\n");
    Run bank = run(base, utf8(base / u8("bank.mml")));
    test::check(has(bank.err, "is codec 'x'") && !has(bank.err, "cannot open"),
                "#pcmbank opens a file whose name is not ASCII: " + bank.err);

    // A source in a folder whose name the old ANSI code page cannot spell.
    const fs::path odd = base / u8("ü😀");
    fs::create_directories(odd);
    writeFile(odd / u8("tune.mml"), "#assign A SSGS 0\nA C4\n");
    Run tune = run(base, utf8(odd / u8("tune.mml")));
    test::check(tune.exit == 0 && fs::exists(odd / u8("TUNE.SQ")),
                "a source in a folder named outside the ANSI code page compiles: " + tune.err);

    // One diagnostic line: the path and the quoted source text in one encoding.
    writeFile(base / u8("誤り.mml"), "#foo日本\n");
    Run wrong = run(base, utf8(base / u8("誤り.mml")));
    test::check(has(wrong.err, "誤り.mml:1:1: error: '#foo日本'"),
                "the path and the quoted source come out in UTF-8 together: " + wrong.err);

    // The output name: one '_' for each character that is not ASCII, and a warning.
    writeFile(base / u8("曲1.mml"), "#assign A SSGS 0\nA C4\n");
    Run song = run(base, utf8(base / u8("曲1.mml")));
    test::check(song.exit == 0 && fs::exists(base / u8("_1.SQ")),
                "曲1.mml writes _1.SQ: " + song.out + song.err);
    test::check(has(song.err, "warning"), "replacing a character is warned about: " + song.err);
    test::check(has(song.out, utf8(base / u8("_1.SQ")) + " ("),
                "the written file is named on stdout: " + song.out);

    fs::remove_all(base);
    return test::report("cli_test");
}
