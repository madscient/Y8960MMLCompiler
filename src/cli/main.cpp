#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "compile.h"
#include "diag.h"

namespace {

void usage() {
    std::cout << "y8mmlc - the Y8960 MML compiler\n"
                 "\n"
                 "  y8mmlc <source.mml> [-o <base name>] [--out-dir <folder>]\n"
                 "\n"
                 "  -o <base name>     the name the outputs take, without an extension\n"
                 "  --out-dir <folder> where to write them; the source's folder by default\n"
                 "  -h, --help         this text\n"
                 "\n"
                 "It writes <NAME>.SQ, the sequence, and <NAME>.PC, the ADPCM samples,\n"
                 "the second only when the source has a #pcmbank.\n";
}

bool writeFile(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
    return static_cast<bool>(out);
}

std::string directoryOf(const std::string& path) {
    std::size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? std::string() : path.substr(0, cut + 1);
}

std::string withSeparator(std::string dir) {
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir.push_back('/');
    return dir;
}

} // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string base;
    std::string outDir;
    bool haveOutDir = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        }
        if (arg == "-o" || arg == "--out-dir") {
            if (i + 1 >= argc) {
                std::cerr << "y8mmlc: " << arg << " needs a value\n";
                return 2;
            }
            if (arg == "-o") {
                base = argv[++i];
            } else {
                outDir = argv[++i];
                haveOutDir = true;
            }
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "y8mmlc: '" << arg << "' is not an option\n";
            return 2;
        }
        if (!input.empty()) {
            std::cerr << "y8mmlc: only one source file at a time\n";
            return 2;
        }
        input = arg;
    }

    if (input.empty()) {
        usage();
        return 2;
    }

    y8::Diagnostics diag;
    y8::CompileResult result;
    bool ok = y8::compileFile(input, result, diag);

    bool truncated = false;
    if (base.empty()) {
        base = y8::outputBaseName(input, truncated);
    } else {
        base = y8::outputBaseName(base, truncated);
    }
    if (truncated) {
        diag.warning(input, 0, 0,
                     "the output name does not fit MSX-DOS's eight characters; it is '" + base +
                         "'");
    }

    for (const y8::Diagnostic& d : diag.all()) std::cerr << d.format() << "\n";
    if (!ok) {
        std::cerr << "y8mmlc: " << diag.errorCount() << " error(s); nothing was written\n";
        return 1;
    }

    const std::string dir = haveOutDir ? withSeparator(outDir) : directoryOf(input);
    const std::string seqPath = dir + base + ".SQ";
    if (!writeFile(seqPath, result.sequence)) {
        std::cerr << "y8mmlc: cannot write '" << seqPath << "'\n";
        return 1;
    }
    std::cout << seqPath << " (" << result.sequence.size() << " bytes)\n";

    if (result.hasPcm) {
        const std::string pcmPath = dir + base + ".PC";
        if (!writeFile(pcmPath, result.pcm)) {
            std::cerr << "y8mmlc: cannot write '" << pcmPath << "'\n";
            return 1;
        }
        std::cout << pcmPath << " (" << result.pcm.size() << " bytes)\n";
    }
    return 0;
}
