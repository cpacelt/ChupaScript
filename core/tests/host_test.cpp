#include <gtest/gtest.h>

#include "host.hpp"

namespace {

bool neverCalled(ChupaContext *, const ChupaValue *, size_t, ChupaValue *,
                 void *) {
    ADD_FAILURE() << "коллбэк звался там, где вызова быть не должно";
    return false;
}

/// Описание, которое проходит все проверки: тесты ниже портят его по одному
/// полю за раз, поэтому исправным он обязан быть ровно один.
///
/// Задача 4 выносит эту функцию в core/tests/host_fixture.hpp — её зовут ещё
/// три файла тестов, и копия в каждом разошлась бы при первой же правке
/// состава ChupaFunction.
ChupaFunction healthyFunction(const char *name) {
    ChupaFunction fn{};
    fn.name = name;
    fn.name_len = std::char_traits<char>::length(name);
    fn.min_args = 1;
    fn.max_args = 1;
    fn.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_PURE | CHUPA_FN_DETERMINISTIC;
    fn.call = neverCalled;
    fn.user_data = nullptr;
    fn.release = nullptr;
    return fn;
}

}  // namespace

TEST(HostTable, AcceptsHealthyDescriptor) {
    CS::HostTable table;
    EXPECT_EQ(table.add(healthyFunction("formatDate")), CS::RegisterOutcome::Ok);
    EXPECT_EQ(table.size(), 1u);
}

TEST(HostTable, RefusesNameThatIsNotAnIdentifier) {
    CS::HostTable table;
    EXPECT_EQ(table.add(healthyFunction("format date")), CS::RegisterOutcome::BadName);
    EXPECT_EQ(table.add(healthyFunction("")), CS::RegisterOutcome::BadName);
    EXPECT_EQ(table.add(healthyFunction("1st")), CS::RegisterOutcome::BadName);
}

TEST(HostTable, RefusesReservedWord) {
    CS::HostTable table;
    EXPECT_EQ(table.add(healthyFunction("return")), CS::RegisterOutcome::BadName);
    EXPECT_EQ(table.add(healthyFunction("null")), CS::RegisterOutcome::BadName);
}

/// Совпадение с билтином отвергается, а не разрешается с приоритетом: иначе
/// count переопределяется хостом и семантика docs/semantics.md §8 перестаёт
/// быть свойством языка.
TEST(HostTable, RefusesNameTakenByBuiltin) {
    CS::HostTable table;
    EXPECT_EQ(table.add(healthyFunction("count")), CS::RegisterOutcome::NameTaken);
    EXPECT_EQ(table.add(healthyFunction("format")), CS::RegisterOutcome::NameTaken);
}

TEST(HostTable, RefusesDuplicateRegistration) {
    CS::HostTable table;
    ASSERT_EQ(table.add(healthyFunction("formatDate")), CS::RegisterOutcome::Ok);
    EXPECT_EQ(table.add(healthyFunction("formatDate")), CS::RegisterOutcome::NameTaken);
    EXPECT_EQ(table.size(), 1u);
}

TEST(HostTable, RefusesNullCallback) {
    CS::HostTable table;
    ChupaFunction fn = healthyFunction("formatDate");
    fn.call = nullptr;
    EXPECT_EQ(table.add(fn), CS::RegisterOutcome::NoCallback);
}

TEST(HostTable, RefusesInvertedArity) {
    CS::HostTable table;
    ChupaFunction fn = healthyFunction("formatDate");
    fn.min_args = 3;
    fn.max_args = 2;
    EXPECT_EQ(table.add(fn), CS::RegisterOutcome::BadArity);
}

TEST(HostTable, AcceptsVariadicArity) {
    CS::HostTable table;
    ChupaFunction fn = healthyFunction("joinAll");
    fn.min_args = 1;
    fn.max_args = CHUPA_VARIADIC;
    EXPECT_EQ(table.add(fn), CS::RegisterOutcome::Ok);
}

/// Детерминированность обещает, что вызов можно пропустить, взяв результат из
/// кэша; грязная функция зовётся ради побочного эффекта, и пропуск его
/// отменяет. Объявить оба — попросить движок пропускать непропускаемое.
TEST(HostTable, RefusesDeterministicWithoutPure) {
    CS::HostTable table;
    ChupaFunction fn = healthyFunction("track");
    fn.flags = CHUPA_FN_DETERMINISTIC;
    EXPECT_EQ(table.add(fn), CS::RegisterOutcome::BadFlags);
}

TEST(HostTable, RefusesWhenFull) {
    CS::HostTable table;
    for (std::uint8_t i = 0; i < CS::kMaxHostFunctions; ++i) {
        const std::string name = "fn" + std::to_string(i);
        ChupaFunction fn = healthyFunction(name.c_str());
        fn.name_len = name.size();
        ASSERT_EQ(table.add(fn), CS::RegisterOutcome::Ok) << "на номере " << int(i);
    }
    EXPECT_EQ(table.add(healthyFunction("oneTooMany")), CS::RegisterOutcome::TableFull);
}

TEST(HostTable, FindsRegisteredByName) {
    CS::HostTable table;
    ASSERT_EQ(table.add(healthyFunction("formatDate")), CS::RegisterOutcome::Ok);
    ASSERT_EQ(table.add(healthyFunction("pluralForm")), CS::RegisterOutcome::Ok);

    std::uint8_t index = 0xff;
    EXPECT_NE(table.find("pluralForm", &index), nullptr);
    EXPECT_EQ(index, 1u);
    EXPECT_EQ(table.at(index).name, "pluralForm");
    EXPECT_EQ(table.find("noSuchName", &index), nullptr);
}

/// release зовётся ровно один раз на каждую функцию, и только при разрушении
/// таблицы: реестр только пополняется, поэтому другого момента не существует.
TEST(HostTable, ReleasesEveryUserDataExactlyOnce) {
    static int released = 0;
    released = 0;
    int firstBox = 0;
    int secondBox = 0;
    {
        CS::HostTable table;
        ChupaFunction a = healthyFunction("first");
        a.user_data = &firstBox;
        a.release = [](void *) { ++released; };
        ChupaFunction b = healthyFunction("second");
        b.user_data = &secondBox;
        b.release = [](void *) { ++released; };
        ASSERT_EQ(table.add(a), CS::RegisterOutcome::Ok);
        ASSERT_EQ(table.add(b), CS::RegisterOutcome::Ok);
        EXPECT_EQ(released, 0);
    }
    EXPECT_EQ(released, 2);
}

/// Отказ не обязан звать release: коробку хост ещё держит сам и освободит её
/// на своей стороне. Позвать её здесь значило бы освободить дважды.
TEST(HostTable, RefusedRegistrationDoesNotRelease) {
    static int released = 0;
    released = 0;
    {
        CS::HostTable table;
        ChupaFunction fn = healthyFunction("count");   // имя занято билтином
        fn.release = [](void *) { ++released; };
        ASSERT_EQ(table.add(fn), CS::RegisterOutcome::NameTaken);
    }
    EXPECT_EQ(released, 0);
}
