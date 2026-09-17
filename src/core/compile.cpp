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
    if (!src.pcmJson.empty()) {
        adpcmOk = readAdpcm(src, adpcm, diag);
    }

    Sequence seq;
    bool seqOk = compileSequence(src, seq, diag);

    // A track that sounds a voice file nothing bound is a key-on with no sample
    // behind it, which is silence on the machine and a typo here.
    if (!src.pcmJson.empty() && adpcmOk) {
        for (int number : seq.adpcmVoiceFiles) {
            bool bound = false;
            for (const VoiceFile& f : adpcm.files) {
                if (f.number == number) {
                    bound = true;
                    break;
                }
            }
            if (!bound) {
                diag.error(src.path, src.pcmLine, 1,
                           "voice file " + std::to_string(number) +
                               " is sounded but no #adpcm binds it");
                adpcmOk = false;
            }
        }
    } else if (src.pcmJson.empty() && !seq.adpcmVoiceFiles.empty()) {
        diag.error(src.path, 0, 0, "an ADPCM track sounds voice files but there is no #pcm");
        adpcmOk = false;
    }

    if (!seqOk || !adpcmOk) return false;

    out.sequence = writeBlock(seq, adpcm);
    if (!src.pcmJson.empty()) {
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

std::string outputBaseName(const std::string& inputPath, bool& truncated) {
    std::size_t slash = inputPath.find_last_of("/\\");
    std::string stem = (slash == std::string::npos) ? inputPath : inputPath.substr(slash + 1);
    std::size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos && dot != 0) stem = stem.substr(0, dot);

    std::string name;
    for (char c : stem) {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) {
            name.push_back(static_cast<char>(std::toupper(u)));
        } else {
            name.push_back('_');
        }
    }
    if (name.empty()) name = "OUTPUT";
    truncated = name.size() > 8;
    if (truncated) name.resize(8);
    return name;
}

} // namespace y8
