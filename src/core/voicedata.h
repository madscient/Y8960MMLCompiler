#pragma once

// The records a sequence has to carry for the voices it names. See the rights
// section of README.md before redistributing: the FM presets are not this
// project's own work.

#include <array>
#include <cstdint>
#include <cstring>

namespace y8 {

constexpr int kVoiceRecordSize = 32;
constexpr int kPresetVoiceCount = 64;  // @0-@63 on OPLLEX and OPL2EX
constexpr int kPresetWaveCount = 16;   // @0-@15 on the SCC
constexpr int kRhythmVoiceCount = 3;   // slots 32-34, OPL2EX's rhythm mode

using VoiceRecord = std::array<std::uint8_t, kVoiceRecordSize>;

VoiceRecord presetVoice(int n);
VoiceRecord rhythmVoice(int n);
VoiceRecord presetWave(int n);

} // namespace y8
