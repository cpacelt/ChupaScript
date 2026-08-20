#include "host.hpp"

#include <cassert>

#include "builtin.hpp"
#include "data.hpp"

namespace CS {

HostTable::~HostTable() {
    for (const HostFunction &fn : functions_) {
        if (fn.release != nullptr) { fn.release(fn.userData); }
    }
}

RegisterOutcome HostTable::add(const ChupaFunction &desc) {
    // Порядок проверок — от дешёвых к дорогим, кроме имени: оно первым,
    // потому что негодное имя чаще всего и есть ошибка хоста, и сообщить
    // хочется именно про него, а не про случайно совпавший второй изъян.
    const std::string_view name(desc.name == nullptr ? "" : desc.name,
                                desc.name == nullptr ? 0 : desc.name_len);
    if (!isGlobalName(name)) { return RegisterOutcome::BadName; }

    Builtin ignored = Builtin::Count;
    if (findBuiltin(name, &ignored)) { return RegisterOutcome::NameTaken; }

    std::uint8_t taken = 0;
    if (find(name, &taken) != nullptr) { return RegisterOutcome::NameTaken; }

    if (desc.call == nullptr) { return RegisterOutcome::NoCallback; }
    if (desc.min_args > desc.max_args) { return RegisterOutcome::BadArity; }

    const bool pure = (desc.flags & CHUPA_FN_PURE) != 0;
    const bool deterministic = (desc.flags & CHUPA_FN_DETERMINISTIC) != 0;
    if (deterministic && !pure) { return RegisterOutcome::BadFlags; }

    if (functions_.size() >= kMaxHostFunctions) {
        return RegisterOutcome::TableFull;
    }

    functions_.push_back(HostFunction{std::string(name), desc.min_args,
                                      desc.max_args, desc.flags, desc.call,
                                      desc.user_data, desc.release});
    return RegisterOutcome::Ok;
}

const HostFunction *HostTable::find(std::string_view name,
                                    std::uint8_t *index) const noexcept {
    for (std::size_t i = 0; i < functions_.size(); ++i) {
        if (functions_[i].name == name) {
            *index = static_cast<std::uint8_t>(i);
            return &functions_[i];
        }
    }
    return nullptr;
}

const HostFunction &HostTable::at(std::uint8_t index) const noexcept {
    assert(index < functions_.size());
    return functions_[index];
}

}  // namespace CS
