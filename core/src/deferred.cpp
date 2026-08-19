#include "deferred.hpp"

namespace CS {

void Deferred::drainSlow() noexcept {
    // Дописать в список освобождение не может: detail::release не видит ни
    // одного хранилища — он рекурсивно зовёт себя и KeyTable::release, и
    // только их. Поэтому обход обычный.
    for (detail::Box *box : refs_) { detail::release(box); }
    refs_.clear();
}

}  // namespace CS
