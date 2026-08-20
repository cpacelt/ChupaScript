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

/// Описание, которое проходит все проверки: тесты ниже портят его по одному
/// полю за раз, поэтому исправным он обязан быть ровно один.
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
