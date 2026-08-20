#include "epoch.hpp"

#include <cassert>

namespace CS {

const Epoch kZeroEpoch = 0;

EpochSlots::~EpochSlots() {
    for (Epoch *block : blocks_) { delete[] block; }
}

void EpochSlots::open(std::uint32_t slot, Epoch birth) {
    assert(slot == count_ && "ячейки заводятся подряд и не удаляются");
    const std::uint32_t block = slot >> kBlockShift;
    if (block == blocks_.size()) {
        // Куски нулями не набиваются: заведённой считается ячейка, до которой
        // дошёл open, и только её вправе читать addressOf.
        blocks_.push_back(new Epoch[kBlockSize]);
    }
    blocks_[block][slot & kBlockMask] = birth;
    count_ = slot + 1;
}

void EpochSlots::bump(std::uint32_t slot, Epoch value) noexcept {
    assert(slot < count_ && "ячейка не заведена");
    blocks_[slot >> kBlockShift][slot & kBlockMask] = value;
}

const Epoch *EpochSlots::addressOf(std::uint32_t slot) const noexcept {
    assert(slot < count_ && "ячейка не заведена");
    return &blocks_[slot >> kBlockShift][slot & kBlockMask];
}

Epoch EpochSlots::at(std::uint32_t slot) const noexcept {
    return *addressOf(slot);
}

}  // namespace CS
