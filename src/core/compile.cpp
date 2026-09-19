#include "compile.h"

#include <cctype>

#include "adpcm.h"
#include "mml.h"
#include "source.h"
#include "writer.h"

namespace y8 {
namespace {

bool build(SourceFile& src, CompileResult& out, Diagnostics& diag) {
    AdpcmData adpcm;
    bool adpcmOk = true;
    if (!src.pcmBankJson.empty()) {
        adpcmOk = readAdpcm(src, adpcm, diag);
    }

    Sequence seq;
    bool seqOk = compileSequence(src, seq, diag);

    // A track that sounds a voice file nothing bound is a key-on with no sample
    // behind it, which is silence on the machine and a typo here.
    if (!src.pcmBankJson.empty() && adpcmOk) {
        for (int number : seq.adpcmVoiceFiles) {
            bool bound = false;
            for (const VoiceFile& f : adpcm.files) {
                if (f.number == number) {
                    bound = true;
                    break;
                }
            }
            if (!bound) {
                diag.error(src.path, src.pcmBankLine, 1,
                           "voice file " + std::to_string(number) +
                               " is sounded but the bank has no entry for it");
                adpcmOk = false;
            }
        }
    } else if (src.pcmBankJson.empty() && !seq.adpcmVoiceFiles.empty()) {
        diag.error(src.path, 0, 0,
                   "an ADPCM track sounds voice files but there is no #pcmbank");
        adpcmOk = false;
    }

    if (!seqOk || !adpcmOk) return false;

    out.sequence = writeBlock(seq, adpcm);
    if (!src.pcmBankJson.empty()) {
        out.pcm = writePcmFile(adpcm);
        out.hasPcm = true;
    }
    return true;
}

} // namespace

bool compileText(const std::string& path, const std::string& text, CompileResult& out,
                 Diagnostics& diag) {
    SourceFile src;
    readSourceText(path, text, src, diag);
    if (diag.hasErrors()) return false;
    return build(src, out, diag);
}

bool compileFile(const std::string& path, CompileResult& out, Diagnostics& diag) {
    SourceFile src;
    readSource(path, src, diag);
    if (diag.hasErrors()) return false;
    return build(src, out, diag);
}

std::string outputBaseName(const std::string& inputPath, bool& truncated, bool& replaced) {
    std::size_t slash = inputPath.find_last_of("/\\");
    std::string stem = (slash == std::string::npos) ? inputPath : inputPath.substr(slash + 1);
    std::size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos && dot != 0) stem = stem.substr(0, dot);

    // The path is UTF-8, and a character that is not ASCII becomes one '_'
    // however many bytes it takes. A byte that does not begin a well formed
    // sequence counts as a character of its own.
    std::string name;
    replaced = false;
    for (std::size_t i = 0; i < stem.size();) {
        unsigned char u = static_cast<unsigned char>(stem[i]);
        if (u < 0x80) {
            name.push_back(std::isalnum(u) ? static_cast<char>(std::toupper(u)) : '_');
            ++i;
            continue;
        }
        std::size_t len = (u >= 0xF0 && u <= 0xF4) ? 4 : (u >= 0xE0) ? 3 : (u >= 0xC2) ? 2 : 1;
        if (u > 0xF4) len = 1;
        for (std::size_t k = 1; k < len; ++k) {
            unsigned char c = (i + k < stem.size()) ? static_cast<unsigned char>(stem[i + k]) : 0;
            if ((c & 0xC0) != 0x80) {
                len = 1;
                break;
            }
        }
        name.push_back('_');
        replaced = true;
        i += len;
    }
    if (name.empty()) name = "OUTPUT";
    truncated = name.size() > 8;
    if (truncated) name.resize(8);
    return name;
}

} // namespace y8
