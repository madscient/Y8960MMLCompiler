#include "writer.h"

namespace y8 {
namespace {

void putWord(std::vector<std::uint8_t>& out, unsigned v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void putChunk(std::vector<std::uint8_t>& out, std::uint8_t type,
              const std::vector<std::uint8_t>& body) {
    out.push_back(type);
    putWord(out, static_cast<unsigned>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
}

constexpr std::uint8_t kChunkTrack = 0x00;
constexpr std::uint8_t kChunkVoice = 0x01;
constexpr std::uint8_t kChunkWave = 0x02;
constexpr std::uint8_t kChunkVoiceFile = 0x03;
constexpr int kRhythmSlotFirst = 32;  // slots 32-34, outside the set events name

} // namespace

std::vector<std::uint8_t> writeBlock(const Sequence& seq, const AdpcmData& adpcm) {
    std::vector<std::uint8_t> out;
    const char* magic = "Y8SQ";
    out.insert(out.end(), magic, magic + 4);
    out.push_back(0x01);
    putWord(out, 0);  // the size, once it is known

    // Every track that holds a channel is written, empty or not: the device and
    // channel are part of the block, so an unwritten part keeps its assignment.
    for (int i = 0; i < kTrackCount; ++i) {
        const TrackCode& t = seq.tracks[i];
        if (!t.assigned) continue;
        std::vector<std::uint8_t> body;
        body.push_back(static_cast<std::uint8_t>(i));
        body.push_back(static_cast<std::uint8_t>(t.device));
        body.push_back(static_cast<std::uint8_t>(t.channel));
        body.insert(body.end(), t.bytes.begin(), t.bytes.end());
        putChunk(out, kChunkTrack, body);
    }

    const std::vector<VoiceSet::Slot>& slots = seq.voices.slots();
    for (std::size_t i = 0; i < slots.size(); ++i) {
        std::vector<std::uint8_t> body;
        body.push_back(static_cast<std::uint8_t>(i));
        body.insert(body.end(), slots[i].record.begin(), slots[i].record.end());
        putChunk(out, slots[i].isWave ? kChunkWave : kChunkVoice, body);
    }

    if (seq.voices.hasRhythmVoices()) {
        for (int i = 0; i < kRhythmVoiceCount; ++i) {
            VoiceRecord record = rhythmVoice(i);
            std::vector<std::uint8_t> body;
            body.push_back(static_cast<std::uint8_t>(kRhythmSlotFirst + i));
            body.insert(body.end(), record.begin(), record.end());
            putChunk(out, kChunkVoice, body);
        }
    }

    // Only the voice files the ADPCM tracks sound, and only those that have a
    // place in the sample memory.
    for (const VoiceFile& f : adpcm.files) {
        if (seq.adpcmVoiceFiles.find(f.number) == seq.adpcmVoiceFiles.end()) continue;
        std::vector<std::uint8_t> body;
        body.push_back(static_cast<std::uint8_t>(f.number));
        putWord(body, static_cast<unsigned>(f.startPage));
        putWord(body, static_cast<unsigned>(f.pageCount));
        putWord(body, static_cast<unsigned>(f.sampleRate));
        putChunk(out, kChunkVoiceFile, body);
    }

    out[5] = static_cast<std::uint8_t>(out.size() & 0xFF);
    out[6] = static_cast<std::uint8_t>((out.size() >> 8) & 0xFF);
    return out;
}

std::vector<std::uint8_t> writePcmFile(const AdpcmData& adpcm) {
    std::vector<std::uint8_t> out;
    const char* magic = "Y8PC";
    out.insert(out.end(), magic, magic + 4);
    out.push_back(0x01);
    out.push_back(0x03);  // bit0 the settings, bit1 the dump
    out.push_back(static_cast<std::uint8_t>(adpcm.files.size()));
    putWord(out, static_cast<unsigned>(adpcm.dump.size() / kPcmPageSize));

    for (const VoiceFile& f : adpcm.files) {
        out.push_back(static_cast<std::uint8_t>(f.number));
        putWord(out, static_cast<unsigned>(f.startPage));
        putWord(out, static_cast<unsigned>(f.pageCount));
        putWord(out, static_cast<unsigned>(f.sampleRate));
    }
    out.insert(out.end(), adpcm.dump.begin(), adpcm.dump.end());
    return out;
}

} // namespace y8
