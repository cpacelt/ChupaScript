#include <gtest/gtest.h>

#include "context.hpp"
#include "host.hpp"
#include "host_fixture.hpp"

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

/// Кэшируемость обещает, что вызов можно пропустить, взяв результат из кэша;
/// функция с эффектом зовётся ради этого эффекта, и пропуск его отменяет.
/// Объявить одну без другой — попросить движок пропускать непропускаемое.
TEST(HostTable, RefusesCacheableWithoutEffectFree) {
    CS::HostTable table;
    ChupaFunction fn = healthyFunction("track");
    fn.flags = CHUPA_FN_CACHEABLE;
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

namespace {

/// Сигнатура сверена с ChupaHostFunction (chupascript.h): пять параметров,
/// argc — size_t, и последний — user_data, которого до этого места брифа не
/// доехало.
bool returnsSeven(ChupaContext *, const ChupaValue *, std::size_t,
                  ChupaValue *out, void *) {
    chupa_make_number(out, 7.0);
    return true;
}

}  // namespace

TEST(HostCacheable, AnExpressionOverACacheableFunctionStaysCacheable) {
    CS::Context ctx;
    // described() (host_fixture.hpp) собирает описание уже с CACHEABLE —
    // ровно тот случай, который здесь и проверяется.
    ChupaFunction seven = described("seven", 0, 0, returnsSeven);
    ASSERT_EQ(ctx.registerFunction(seven), CS::RegisterOutcome::Ok);

    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(ctx.compileExpression("seven()", &expr, diags, 4), 0u);

    EXPECT_TRUE(expr.isCacheable());
}

TEST(HostCacheable, AnExpressionOverAClockIsNotCacheable) {
    // now() и screenWidth() — «без эффектов, но некэшируемая»
    // (docs/semantics.md §8.9). Набор зависимостей у такого выражения пуст, и
    // без этой отметки оно кэшировалось бы навсегда: часы бы встали.
    //
    // Плейсхолдер format — позиционный ${} (docs/semantics.md §8.8), не
    // встроенное выражение: now() подставляется как отдельный аргумент, а не
    // текстом внутри шаблона (${now()} без аргумента был бы литералом без
    // единого вызова).
    CS::Context ctx;
    ChupaFunction now = described("now", 0, 0, returnsSeven);
    now.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_EFFECT_FREE;  // без CACHEABLE
    ASSERT_EQ(ctx.registerFunction(now), CS::RegisterOutcome::Ok);

    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(ctx.compileExpression("format('${}', now())", &expr, diags, 4), 0u)
        << diags[0].message;

    EXPECT_FALSE(expr.isCacheable());
}

TEST(HostCacheable, TheMarkSurvivesDepth) {
    // Вызов может сидеть где угодно в дереве: отметка на дереве, а не на
    // узле.
    CS::Context ctx;
    ChupaFunction now = described("now", 0, 0, returnsSeven);
    now.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_EFFECT_FREE;
    ASSERT_EQ(ctx.registerFunction(now), CS::RegisterOutcome::Ok);

    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(ctx.compileExpression("1 + (2 * now())", &expr, diags, 4), 0u);

    EXPECT_FALSE(expr.isCacheable());
}

TEST(HostCacheable, RecompilationResetsTheMark) {
    // Отметка живёт на дереве (Ast) и обязана сбрасываться в Ast::reset —
    // Ast пригоден для повторного разбора, и без сброса перекомпиляция того
    // же Expression из некэшируемого исходника в кэшируемый унаследовала бы
    // старый вердикт.
    CS::Context ctx;
    ChupaFunction now = described("now", 0, 0, returnsSeven);
    now.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_EFFECT_FREE;
    ASSERT_EQ(ctx.registerFunction(now), CS::RegisterOutcome::Ok);

    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(ctx.compileExpression("now()", &expr, diags, 4), 0u);
    ASSERT_FALSE(expr.isCacheable());

    ASSERT_EQ(ctx.compileExpression("1 + 1", &expr, diags, 4), 0u);
    EXPECT_TRUE(expr.isCacheable());
}

TEST(BuiltinsAreCacheable, EveryBuiltinReachableFromAnExpressionIsCacheable) {
    // Билтины детерминированы: часов и флагов среди них нет, а мутирующие
    // Push и Pop до выражения не доходят — их результат Void, и §6.2
    // отвергает такое выражение раньше.
    CS::Context ctx;
    CS::Diagnostic diags[4];
    CS::Expression expr;
    ASSERT_EQ(ctx.compileExpression("count([1, 2, 3])", &expr, diags, 4), 0u);

    EXPECT_TRUE(expr.isCacheable());
}
