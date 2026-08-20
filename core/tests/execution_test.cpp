#include <gtest/gtest.h>

#include <cstdint>

#include "execution.hpp"
#include "host.hpp"
#include "store.hpp"
#include "value.hpp"

/// Аргумент внешнего вызова сам может быть вызовом, и вложенный занимает
/// стек раньше, чем внешний собрал свои. Один общий буфер здесь отдал бы
/// внешнему вызову то, что записал вложенный.
TEST(ArgFrame, NestedFrameDoesNotDisturbTheOuterOne) {
    CS::Store store;
    CS::Execution exec(store);

    CS::ArgFrame outer(exec, 2);
    outer[0] = CS::Value::number(1.0);
    {
        CS::ArgFrame inner(exec, 3);
        inner[0] = CS::Value::number(100.0);
        inner[1] = CS::Value::number(200.0);
        inner[2] = CS::Value::number(300.0);
        EXPECT_EQ(inner.size(), 3u);
    }
    outer[1] = CS::Value::number(2.0);

    EXPECT_EQ(outer.size(), 2u);
    EXPECT_EQ(outer.data()[0].numberValue(), 1.0);
    EXPECT_EQ(outer.data()[1].numberValue(), 2.0);
}

/// Вариадичность исключает любой фиксированный потолок: kMaxFixedArgs
/// рассчитан на два, а хост вправе объявить сколько угодно.
TEST(ArgFrame, HoldsMoreThanTheBuiltinCeiling) {
    CS::Store store;
    CS::Execution exec(store);

    CS::ArgFrame frame(exec, 64);
    for (std::uint32_t i = 0; i < 64; ++i) {
        frame[i] = CS::Value::number(static_cast<double>(i));
    }
    for (std::uint32_t i = 0; i < 64; ++i) {
        EXPECT_EQ(frame.data()[i].numberValue(), static_cast<double>(i));
    }
}

TEST(ArgFrame, EmptyFrameIsUsable) {
    CS::Store store;
    CS::Execution exec(store);
    CS::ArgFrame frame(exec, 0);
    EXPECT_EQ(frame.size(), 0u);
}

/// Проводка таблицы — часть устройства, а не отдельное действие, которое
/// можно забыть: раньше здесь был сеттер, и его забыли позвать.
TEST(Execution, CarriesTheHostTableItWasBuiltWith) {
    CS::Store store;
    CS::HostTable hosts;
    CS::Execution wired(store, &hosts);
    EXPECT_EQ(wired.hosts(), &hosts);

    CS::Execution bare(store);
    EXPECT_EQ(bare.hosts(), nullptr);
}

TEST(DepSet, RecordsUpToTheCeiling) {
    CS::DepSet deps;
    CS::Epoch a = 1, b = 2, c = 3, d = 4;
    deps.add(&a, CS::Value::null());
    deps.add(&b, CS::Value::null());
    deps.add(&c, CS::Value::null());
    deps.add(&d, CS::Value::null());

    EXPECT_EQ(deps.count(), CS::kMaxDeps);
    EXPECT_FALSE(deps.overflowed());
}

TEST(DepSet, OneMoreThanTheCeilingIsOverflow) {
    // Направление огрубления безопасное: лишний пересчёт, но никогда ложное
    // попадание (спека §2.3).
    CS::DepSet deps;
    CS::Epoch words[5] = {1, 2, 3, 4, 5};
    for (CS::Epoch &word : words) { deps.add(&word, CS::Value::null()); }

    EXPECT_TRUE(deps.overflowed());
}

TEST(DepSet, TheSameAddressTwiceTakesOneSlot) {
    // a.x + a.y трогает ячейку a дважды и коробку a дважды. Без слияния такое
    // выражение переполнялось бы на ровном месте.
    CS::DepSet deps;
    CS::Epoch a = 1, b = 2;
    deps.add(&a, CS::Value::null());
    deps.add(&b, CS::Value::null());
    deps.add(&a, CS::Value::null());

    EXPECT_EQ(deps.count(), 2u);
}

TEST(DepSet, ResetForgetsEverythingIncludingOverflow) {
    CS::DepSet deps;
    CS::Epoch words[5] = {1, 2, 3, 4, 5};
    for (CS::Epoch &word : words) { deps.add(&word, CS::Value::null()); }

    deps.reset();

    EXPECT_EQ(deps.count(), 0u);
    EXPECT_FALSE(deps.overflowed());
}
