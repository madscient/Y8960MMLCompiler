#pragma once

#include <vector>

#include "opcodes.h"
#include "voicedata.h"

namespace y8 {

// The voices a sequence carries, which is what EV_SEQVOICE indexes. A number
// that names the same record twice takes one slot, and the slot is what the
// track names from then on.
class VoiceSet {
public:
    struct Slot {
        bool isWave = false;  // chunk 02 rather than chunk 01
        int number = 0;       // the @n it came from, for the dedup
        VoiceRecord record{};
    };

    // Returns the slot, or -1 when all kVoiceSlots are taken.
    int intern(bool isWave, int number, const VoiceRecord& record);

    const std::vector<Slot>& slots() const { return slots_; }

    // OPL2EX loads three voices of its own in rhythm mode. They sit outside
    // the set because no event names them.
    void needRhythmVoices() { rhythmVoices_ = true; }
    bool hasRhythmVoices() const { return rhythmVoices_; }

private:
    std::vector<Slot> slots_;
    bool rhythmVoices_ = false;
};

} // namespace y8
