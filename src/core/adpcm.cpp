#include "adpcm.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "json.h"

namespace y8 {
namespace {

// The directory part of `path`, with its separator, or empty.
std::string directoryOf(const std::string& path) {
    std::size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? std::string() : path.substr(0, cut + 1);
}

bool isAbsolute(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '\\') return true;
    return path.size() >= 2 && path[1] == ':';
}

std::string resolve(const std::string& base, const std::string& path) {
    return isAbsolute(path) ? path : directoryOf(base) + path;
}

std::string replaceExtension(const std::string& path, const std::string& ext) {
    std::size_t slash = path.find_last_of("/\\");
    std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return path + ext;
    }
    return path.substr(0, dot) + ext;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}

} // namespace

bool readAdpcm(const SourceFile& src, AdpcmData& out, Diagnostics& diag) {
    const std::string jsonPath = resolve(src.path, src.pcmJson);
    std::string jsonText;
    if (!readFile(jsonPath, jsonText)) {
        diag.error(src.path, src.pcmLine, 1, "cannot open '" + jsonPath + "'");
        return false;
    }

    JsonValue root;
    std::string error;
    if (!parseJson(jsonText, root, error)) {
        diag.error(jsonPath, 0, 0, error);
        return false;
    }
    if (!root.isObject()) {
        diag.error(jsonPath, 0, 0, "the top level of an adpcm_packer JSON is an object");
        return false;
    }

    // Y8960's ADPCM reads this one shape and no other.
    std::string codec;
    const JsonValue* codecValue = root.member("codec");
    if (!codecValue || !codecValue->asString(codec) || codec != "adpcm-b") {
        diag.error(src.path, src.pcmLine, 1,
                   "'" + jsonPath + "' is codec '" + codec + "'; Y8960 reads 'adpcm-b'");
        return false;
    }
    long boundary = 0;
    const JsonValue* boundaryValue = root.member("boundary");
    if (!boundaryValue || !boundaryValue->asLong(boundary) || boundary != kPcmPageSize) {
        diag.error(src.path, src.pcmLine, 1,
                   "'" + jsonPath + "' is packed on a " + std::to_string(boundary) +
                       " byte boundary; Y8960 needs " + std::to_string(kPcmPageSize));
        return false;
    }
    long rate = 0;
    const JsonValue* rateValue = root.member("sample_rate");
    if (!rateValue || !rateValue->asLong(rate)) {
        diag.error(jsonPath, 0, 0, "'sample_rate' is missing");
        return false;
    }
    if (rate < kPcmRateMin || rate > kPcmRateMax) {
        diag.error(src.path, src.pcmLine, 1,
                   "the sample rate is " + std::to_string(rate) + "Hz; Y8960 takes " +
                       std::to_string(kPcmRateMin) + " to " + std::to_string(kPcmRateMax));
        return false;
    }

    const JsonValue* entries = root.member("entries");
    if (!entries || !entries->isArray()) {
        diag.error(jsonPath, 0, 0, "'entries' is missing");
        return false;
    }

    // The dump is the packed binary as it stands: an entry's offset is where it
    // sits in the sample memory, so nothing may move.
    const std::string binPath = replaceExtension(jsonPath, ".bin");
    std::string bin;
    if (!readFile(binPath, bin)) {
        diag.error(src.path, src.pcmLine, 1, "cannot open '" + binPath + "'");
        return false;
    }
    std::size_t pages = (bin.size() + kPcmPageSize - 1) / kPcmPageSize;
    if (pages > static_cast<std::size_t>(kPcmPagesMax)) {
        diag.error(src.path, src.pcmLine, 1,
                   "'" + binPath + "' is " + std::to_string(bin.size()) +
                       " bytes; the sample memory holds " +
                       std::to_string(kPcmPagesMax * kPcmPageSize));
        return false;
    }
    out.dump.assign(bin.begin(), bin.end());
    out.dump.resize(pages * kPcmPageSize, 0);

    bool ok = true;
    for (const SampleBinding& binding : src.samples) {
        const JsonValue* found = nullptr;
        for (const JsonValue& entry : entries->array) {
            std::string name;
            const JsonValue* nameValue = entry.member("name");
            if (nameValue && nameValue->asString(name) && name == binding.entry) {
                found = &entry;
                break;
            }
        }
        if (!found) {
            diag.error(src.path, binding.line, 1,
                       "'" + jsonPath + "' has no entry named '" + binding.entry + "'");
            ok = false;
            continue;
        }

        long offset = 0, padded = 0;
        const JsonValue* offsetValue = found->member("offset");
        const JsonValue* paddedValue = found->member("padded_size");
        if (!offsetValue || !offsetValue->asLong(offset) || !paddedValue ||
            !paddedValue->asLong(padded)) {
            diag.error(src.path, binding.line, 1,
                       "entry '" + binding.entry + "' has no offset or padded_size");
            ok = false;
            continue;
        }
        if (offset % kPcmPageSize != 0 || padded % kPcmPageSize != 0) {
            diag.error(src.path, binding.line, 1,
                       "entry '" + binding.entry + "' does not start and end on a page");
            ok = false;
            continue;
        }
        long startPage = offset / kPcmPageSize;
        long pageCount = padded / kPcmPageSize;
        if (pageCount < 1 || startPage + pageCount > kPcmPagesMax) {
            diag.error(src.path, binding.line, 1,
                       "entry '" + binding.entry + "' does not fit in the sample memory");
            ok = false;
            continue;
        }

        // ymz280/opl4/ssgs put a rate on the entry; adpcm-b does not, so the
        // top level one is the rate. Reading it here anyway costs nothing and
        // keeps a hand written JSON honest.
        long entryRate = rate;
        const JsonValue* entryRateValue = found->member("sample_rate");
        if (entryRateValue && entryRateValue->asLong(entryRate)) {
            if (entryRate < kPcmRateMin || entryRate > kPcmRateMax) {
                diag.error(src.path, binding.line, 1,
                           "entry '" + binding.entry + "' is " + std::to_string(entryRate) +
                               "Hz; Y8960 takes " + std::to_string(kPcmRateMin) + " to " +
                               std::to_string(kPcmRateMax));
                ok = false;
                continue;
            }
        }

        out.files.push_back({binding.number, static_cast<int>(startPage),
                             static_cast<int>(pageCount), static_cast<int>(entryRate)});
    }

    std::sort(out.files.begin(), out.files.end(),
              [](const VoiceFile& a, const VoiceFile& b) { return a.number < b.number; });
    return ok;
}

} // namespace y8
