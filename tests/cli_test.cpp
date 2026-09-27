// Runs the y8mmlc and y8mmld executables themselves on files whose names and
// contents are not ASCII. What makes those work on Windows is the executables'
// manifest, which a test that calls the core library in-process would never see.

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

// Runs an executable with arguments already quoted, the way a shell would hand
// them over: as text, not as bytes in some code page.
Run runExe(const fs::path& dir, const char* exe, const std::string& args) {
    const fs::path outFile = dir / "stdout.txt";
    const fs::path errFile = dir / "stderr.txt";
    std::string cmd = "\"" + std::string(exe) + "\" " + args + " > \"" + utf8(outFile) +
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

std::string quote(const std::string& s) { return "\"" + s + "\""; }

Run run(const fs::path& dir, const std::string& arg) { return runExe(dir, Y8MMLC_EXE, quote(arg)); }

bool has(const std::string& text, const std::string& piece) {
    return text.find(piece) != std::string::npos;
}

// y8mmld: it will not write over a file unless told to, and it finds the Y8PC
// y8mmlc wrote beside the sequence - in a folder whose name is not ASCII too.
void decompiler(const fs::path& base) {
    const fs::path dir = base / u8("逆");
    fs::create_directories(dir);
    writeFile(dir / "a.mml", "#assign A SSGS 0\nA C4\n");
    runExe(dir, Y8MMLC_EXE, quote(utf8(dir / "a.mml")) + " -o X");
    writeFile(dir / "X.mml", "kept\n");
    Run refused = runExe(dir, Y8MMLD_EXE, quote(utf8(dir / "X.SQ")));
    test::check(refused.exit == 1 && readFile(dir / "X.mml") == "kept\n",
                "y8mmld does not write over a file that is there: " + refused.err);
    Run forced = runExe(dir, Y8MMLD_EXE, quote(utf8(dir / "X.SQ")) + " --force");
    test::check(forced.exit == 0 && has(readFile(dir / "X.mml"), "#assign A SSGS 0"),
                "--force writes over it: " + forced.err);

    const std::string bank =
        (fs::u8path(Y8MMLC_SOURCE_DIR) / "presets" / "wavs_y8950_adpcmb_excerpt.json").generic_u8string();
    writeFile(dir / "psrc.mml", "#pcmbank " + bank + "\n#assign A OPL2EX1 9\nA @3 c\n");
    runExe(dir, Y8MMLC_EXE, quote(utf8(dir / "psrc.mml")) + " -o P");
    Run adpcm = runExe(dir, Y8MMLD_EXE, quote(utf8(dir / "P.SQ")));
    test::check(adpcm.exit == 0 && has(readFile(dir / "P.mml"), "#pcmbank P.PC\n"),
                "y8mmld takes the Y8PC beside the sequence: " + adpcm.err);
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

    decompiler(base);

    fs::remove_all(base);
    return test::report("cli_test");
}
