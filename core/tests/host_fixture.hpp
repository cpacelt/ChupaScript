#pragma once

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include "host.hpp"

namespace {

bool neverCalled(ChupaContext *, const ChupaValue *, size_t, ChupaValue *,
                 void *) {
    ADD_FAILURE() << "коллбэк звался там, где вызова быть не должно";
    return false;
}

/// Описание с заданной арностью и коллбэком.
///
/// healthyFunction ниже — частный случай этой функции: двух сборщиков
/// описания среди тестовых заголовков быть не должно.
ChupaFunction described(const char *name, std::uint8_t minArgs,
                        std::uint8_t maxArgs, ChupaHostFunction call,
                        void *userData = nullptr) {
    ChupaFunction fn{};
    fn.name = name;
    fn.name_len = std::char_traits<char>::length(name);
    fn.min_args = minArgs;
    fn.max_args = maxArgs;
    fn.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_EFFECT_FREE | CHUPA_FN_CACHEABLE;
    fn.call = call;
    fn.user_data = userData;
    return fn;
}

/// Описание, которое проходит все проверки: тесты ниже портят его по одному
/// полю за раз, поэтому исправным он обязан быть ровно один.
ChupaFunction healthyFunction(const char *name) {
    return described(name, 1, 1, neverCalled);
}

}  // namespace
