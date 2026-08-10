#pragma once
#include <cstddef>
#include <cstdint>
namespace CS {

/// A dynamically typed value used by the ChupaScript runtime.
///
/// Values are non-owning. String, Object, and Array values reference storage
/// owned by the Context from which they were created.
///
/// Using a Value after its owning Context has been destroyed is undefined
/// behavior if the Value references String, Object, or Array storage.
///

struct Value {
    /// Identifies the type of the stored value.
    enum class Kind : std::uint8_t {
        Null,
        Boolean,
        Number,
        String,
        Object,
        Array
    };
    /// Creates a null value.
    [[nodiscard]]
    static Value null() noexcept {
        return Value(Kind::Null);
    }
    /// Creates a boolean value.
    [[nodiscard]]
    static Value boolean(bool value) noexcept {
        return Value(Kind::Boolean, value);
    }
    /// Creates a numeric value.
    [[nodiscard]]
    static Value number(double value) noexcept {
        return Value(Kind::Number, value);
    }
    /// Creates a non-owning reference to String storage.
    [[nodiscard]]
    static Value string(std::size_t* value) noexcept {
        return Value(Kind::String, value);
    }
    /// Creates a non-owning reference to Object storage.
    [[nodiscard]]
    static Value object(std::size_t* value) noexcept {
        return Value(Kind::Object, value);
    }
    /// Creates a non-owning reference to Array storage.
    [[nodiscard]]
    static Value array(std::size_t* value) noexcept {
        return Value(Kind::Array, value);
    }
    /// Returns the kind of the stored value.
    [[nodiscard]]
    Kind kind() const noexcept {
        return kind_;
    }
private:
    explicit Value(Kind kind) noexcept
        : kind_(kind) {}
    Value(Kind kind, bool value) noexcept
        : kind_(kind), boolean_(value) {}
    Value(Kind kind, double value) noexcept
        : kind_(kind), number_(value) {}
    Value(Kind kind, std::size_t* value) noexcept
        : kind_(kind) {
        switch (kind) {
            case Kind::String:
                string_ = value;
                break;
            case Kind::Object:
                object_ = value;
                break;
            case Kind::Array:
                array_ = value;
                break;
            default:
                break;
        }
    }
    Kind kind_;
    // TODO: Consider NaN-boxing or another compact representation if the
    //       size of Value becomes a significant constraint.
    union {
        bool boolean_;
        double number_;
        std::size_t* string_;
        std::size_t* object_;
        std::size_t* array_;
    };
};

/// Compares two values using ChupaScript strict equality semantics.
///
/// This corresponds to the `===` operator.
[[nodiscard]]
bool strictEqual(const Value& lhs, const Value& rhs) noexcept;

/// Compares two values using ChupaScript loose equality semantics.
///
/// This corresponds to the `==` operator.
[[nodiscard]]
bool looseEqual(const Value& lhs, const Value& rhs) noexcept;


} // namespace CS
