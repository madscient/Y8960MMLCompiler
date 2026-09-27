#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "adpcm.h"
#include "compile.h"
#include "console.h"
#include "decompile.h"
#include "diag.h"

namespace fs = std::filesystem;

namespace {

void usage() {
    std::cout << "y8mmld - turns a Y8960 sequence back into MML\n"
                 "\n"
                 "  y8mmld <sequence.SQ> [-o <file>] [--out-dir <folder>] [--pcm <file>] [--force]\n"
                 "\n"
                 "  -o <name>          the MML file's name; <stem>.mml by default\n"
                 "  --out-dir <folder> where to write it; the sequence's folder by default\n"
                 "  --pcm <file>       the Y8PC that holds the ADPCM samples; <stem>.PC\n"
                 "                     beside the sequence is taken when it is there\n"
                 "  --force            write over a file that is already there\n"
                 "  -h, --help         this text\n";
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}

bool exists(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return static_cast<bool>(in);
}

std::string directoryOf(const std::string& path) {
    std::size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? std::string() : path.substr(0, cut + 1);
}

std::string stemOf(const std::string& path) {
    std::size_t cut = path.find_last_of("/\\");
    std::string name = cut == std::string::npos ? path : path.substr(cut + 1);
    std::size_t dot = name.find_last_of('.');
    return (dot == std::string::npos || dot == 0) ? name : name.substr(0, dot);
}

std::string withSeparator(std::string dir) {
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir.push_back('/');
    return dir;
}

// The Y8PC's path as #pcmbank will read it: relative to the MML file.
std::string relativeTo(const std::string& target, const std::string& mmlPath) {
    std::error_code ec;
    const fs::path from = fs::absolute(fs::u8path(mmlPath), ec).parent_path();
    const fs::path to = fs::absolute(fs::u8path(target), ec);
    if (ec) return target;
    fs::path rel = to.lexically_relative(from);
    if (rel.empty()) return to.generic_u8string();
    return rel.generic_u8string();
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    ConsoleUtf8 console;
#endif
    std::string input;
    std::string output;
    std::string outDir;
    std::string pcmPath;
    bool force = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        }
        if (arg == "--force") {
            force = true;
            continue;
        }
        if (arg == "-o" || arg == "--out-dir" || arg == "--pcm") {
            if (i + 1 >= argc) {
                std::cerr << "y8mmld: " << arg << " needs a value\n";
                return 2;
            }
            std::string& into = arg == "-o" ? output : arg == "--out-dir" ? outDir : pcmPath;
            into = argv[++i];
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "y8mmld: '" << arg << "' is not an option\n";
            return 2;
        }
        if (!input.empty()) {
            std::cerr << "y8mmld: only one sequence file at a time\n";
            return 2;
        }
        input = arg;
    }
    if (input.empty()) {
        usage();
        return 2;
    }

    std::string blockBytes;
    if (!readFile(input, blockBytes)) {
        std::cerr << "y8mmld: cannot open '" << input << "'\n";
        return 1;
    }

    const std::string dir = outDir.empty() ? directoryOf(input) : withSeparator(outDir);
    // -o names the file, and it lands where the default would, as y8mmlc's does.
    const std::string mmlPath = dir + (output.empty() ? stemOf(input) + ".mml" : output);
    if (!force && exists(mmlPath)) {
        std::cerr << "y8mmld: '" << mmlPath << "' is already there; --force writes over it\n";
        return 1;
    }

    // A Y8PC named on the command line has to be good. One found beside the
    // sequence is only taken up if it is.
    y8::AdpcmData pcm;
    bool havePcm = false;
    std::string pcmUsed;
    if (!pcmPath.empty()) {
        std::string bytes, error;
        if (!readFile(pcmPath, bytes)) {
            std::cerr << "y8mmld: cannot open '" << pcmPath << "'\n";
            return 1;
        }
        if (!y8::parsePcmFile(bytes, pcm, error)) {
            std::cerr << pcmPath << ": error: " << error << "\n";
            return 1;
        }
        havePcm = true;
        pcmUsed = pcmPath;
    } else {
        for (const char* ext : {".PC", ".pc"}) {
            const std::string beside = directoryOf(input) + stemOf(input) + ext;
            std::string bytes, error;
            if (!readFile(beside, bytes)) continue;
            if (y8::parsePcmFile(bytes, pcm, error)) {
                havePcm = true;
                pcmUsed = beside;
            }
            break;
        }
    }

    y8::DecompileOptions opt;
    opt.name = input;
    if (havePcm) {
        opt.pcm = &pcm;
        opt.pcmBankPath = relativeTo(pcmUsed, mmlPath);
    }

    y8::Diagnostics diag;
    std::string mml;
    const std::vector<std::uint8_t> block(blockBytes.begin(), blockBytes.end());
    bool ok = y8::decompileBlock(block, opt, mml, diag);
    for (const y8::Diagnostic& d : diag.all()) std::cerr << d.format() << "\n";
    if (!ok) {
        std::cerr << "y8mmld: nothing was written\n";
        return 1;
    }

    // What it writes has to compile: a file that does not is no use to anyone.
    // Most often it is a track that the tied notes have pushed past its limit.
    y8::Diagnostics check;
    y8::CompileResult compiled;
    if (!y8::compileText(mmlPath, mml, compiled, check)) {
        std::cerr << "y8mmld: the MML made from '" << input
                  << "' does not compile, so nothing was written\n";
        for (const y8::Diagnostic& d : check.all()) std::cerr << "  " << d.format() << "\n";
        return 1;
    }

    std::ofstream out(mmlPath, std::ios::binary | std::ios::trunc);
    out << mml;
    if (!out) {
        std::cerr << "y8mmld: cannot write '" << mmlPath << "'\n";
        return 1;
    }
    std::cout << mmlPath << "\n";
    return 0;
}
