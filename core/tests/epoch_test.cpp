#include <gtest/gtest.h>

#include <vector>

#include "epoch.hpp"

namespace {

using CS::Epoch;
using CS::EpochClock;
using CS::EpochSlots;

TEST(EpochClock, HandsOutStrictlyGrowingNumbers) {
    EpochClock clock;
    const Epoch first = clock.tick();
    const Epoch second = clock.tick();
    EXPECT_GT(first, 0u) << "ноль не выдаётся никогда: он занят вечным нулём";
    EXPECT_GT(second, first);
}

TEST(EpochClock, NowDoesNotAdvance) {
    EpochClock clock;
    const Epoch issued = clock.tick();
    EXPECT_EQ(clock.now(), issued);
    EXPECT_EQ(clock.now(), issued);
}

TEST(EpochSlots, ZeroIsNeverHandedOut) {
    // На вечный ноль смотрит всякая незаполненная зависимость, и он обязан
    // отличаться от любого выданного номера.
    EXPECT_EQ(CS::kZeroEpoch, 0u);
}

TEST(EpochSlots, AddressSurvivesEveryLaterSlot) {
    // Это и есть причина кусочного хранения: параллельный вектор переехал бы
    // на первом же новом имени, и адрес, отданный обёртке, провис бы (§2.2).
    EpochClock clock;
    EpochSlots slots;
    slots.open(0, clock.tick());
    const Epoch *first = slots.addressOf(0);

    for (std::uint32_t i = 1; i < 1000; ++i) { slots.open(i, clock.tick()); }

    EXPECT_EQ(slots.addressOf(0), first);
    EXPECT_EQ(*first, slots.at(0));
}

TEST(EpochSlots, BumpIsSeenThroughTheAddress) {
    EpochClock clock;
    EpochSlots slots;
    slots.open(0, clock.tick());
    const Epoch *address = slots.addressOf(0);
    const Epoch before = *address;

    slots.bump(0, clock.tick());

    EXPECT_GT(*address, before);
}

TEST(EpochSlots, EveryOpenedSlotHasItsOwnAddress) {
    EpochClock clock;
    EpochSlots slots;
    std::vector<const Epoch *> seen;
    for (std::uint32_t i = 0; i < 200; ++i) {
        slots.open(i, clock.tick());
        seen.push_back(slots.addressOf(i));
    }
    for (std::uint32_t i = 0; i < seen.size(); ++i) {
        EXPECT_EQ(*seen[i], slots.at(i));
        for (std::uint32_t j = i + 1; j < seen.size(); ++j) {
            EXPECT_NE(seen[i], seen[j]);
        }
    }
}

}  // namespace
