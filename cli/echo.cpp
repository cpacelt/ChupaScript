#include "echo.hpp"

#include <cassert>
#include <cstddef>

#include "chupascript/chupascript.h"

namespace chupa {
namespace {

/// Коллбэк, который движок зовёт на `echo(x)`.
///
/// ctx здесь nullptr: оболочка регистрирует напрямую через
/// CS::Context::registerFunction (см. registerEcho ниже), а тот — в отличие
/// от chupa_context_create — непрозрачный указатель C API не проставляет.
/// Чтобы прочитать args[0], контекст не нужен вовсе, поэтому nullptr
/// безвреден; коллбэку, который попробовал бы ctx употребить, он вреден был
/// бы очень.
bool echoCallback(ChupaContext * /*ctx*/, const ChupaValue *args,
                  std::size_t /*argc*/, ChupaValue * /*out*/,
                  void *userData) {
    const char *bytes = nullptr;
    std::size_t len = 0;
    chupa_value_string(&args[0], &bytes, &len);
    auto *stream = static_cast<std::ostream *>(userData);
    stream->write(bytes, static_cast<std::streamsize>(len));
    *stream << "\n";
    return true;
}

}  // namespace

void registerEcho(CS::Context &ctx, std::ostream &out) {
    ChupaFunction desc{};
    desc.name = "echo";
    desc.name_len = 4;
    desc.min_args = 1;
    desc.max_args = 1;
    // Нет RETURNS_VALUE — значит Void; нет EFFECT_FREE — значит с эффектом.
    desc.flags = CHUPA_FN_NONE;
    desc.call = echoCallback;
    desc.user_data = &out;
    desc.release = nullptr;  // out заимствован, освобождать нечего

    // Отрабатывает один раз на свежепостроенном ctx, до всякой компиляции на
    // нём, — единственный случай, в котором registerFunction по описанию
    // отказывает (TooLate), здесь случиться не может, поэтому исход стоит
    // утверждения, а не ветки.
    [[maybe_unused]] const CS::RegisterOutcome outcome =
        ctx.registerFunction(desc);
    assert(outcome == CS::RegisterOutcome::Ok);
}

}  // namespace chupa
