#pragma once

// The records a sequence has to carry for the voices it names. See the rights
// section of README.md before redistributing: the FM presets are not this
// project's own work.

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace y8 {

constexpr int kVoiceRecordSize = 32;
constexpr int kPackedVoiceSize = 12;   // chunk 01: an FM voice as a block carries it
constexpr int kPresetVoiceCount = 64;  // @0-@63 on OPLLEX and OPL2EX
constexpr int kPresetWaveCount = 16;   // @0-@15 on the SCC
constexpr int kRhythmVoiceCount = 3;   // slots 32-34, OPL2EX's rhythm mode

// An SCC waveform, an FM voice in the MSX-AUDIO layout the preset table is
// written in, or a packed voice in its first voiceFormatSize bytes.
using VoiceRecord = std::array<std::uint8_t, kVoiceRecordSize>;
// An FM voice in the register image of bytecode.md's chunk 01.
using PackedVoice = std::array<std::uint8_t, kPackedVoiceSize>;

VoiceRecord presetVoice(int n);
PackedVoice rhythmVoice(int n);
VoiceRecord presetWave(int n);

// The ROM's TAB_VOIPACK: the name, the spare bytes, the velocity byte and
// MSX-AUDIO's flags are dropped, and the transpose rounds to whole semitones.
PackedVoice packVoice(const VoiceRecord& audio);

// The layout a #voice is written in. The packed ones are named after the chunk
// that carries them: OPL chunk 01, OPM chunk 40, OPL3 chunk 42. The others are
// what a BASIC's voice array holds, and are packed as they are read.
enum class VoiceFormat { Opl, Opm, Opl3, Audio, Sfg, Makoto };

int voiceFormatSize(VoiceFormat f);  // how many values a #voice of it takes
const char* voiceFormatSymbol(VoiceFormat f);
// Takes "OPL", "makoto" and so on.
bool parseVoiceFormat(const std::string& text, VoiceFormat& out);
// The symbols parseVoiceFormat takes.
std::vector<std::string> voiceFormatNames();

// The packed format a voice of `f` is carried in: itself, or OPL for AUDIO and
// OPM for SFG and MAKOTO.
VoiceFormat packedFormat(VoiceFormat f);
// `in` holds voiceFormatSize(f) values. The packed record goes into the first
// voiceFormatSize(packedFormat(f)) bytes of the result, and the rest are 0.
VoiceRecord packRecord(VoiceFormat f, const std::vector<std::uint8_t>& in);

} // namespace y8
