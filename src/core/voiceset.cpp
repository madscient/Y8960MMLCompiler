#include "voiceset.h"

namespace y8 {

int VoiceSet::intern(bool isWave, int number, const VoiceRecord& record) {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].isWave == isWave && slots_[i].number == number) return static_cast<int>(i);
    }
    if (slots_.size() >= static_cast<std::size_t>(kVoiceSlots)) return -1;
    slots_.push_back({isWave, number, record});
    return static_cast<int>(slots_.size()) - 1;
}

} // namespace y8
