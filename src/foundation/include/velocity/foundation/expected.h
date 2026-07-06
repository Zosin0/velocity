#pragma once
// Minimal Expected<T, E> (std::expected is C++23; docs/13 mandates result
// types over exceptions in engine/media/gpu code). Intentionally small:
// value-or-error, no monadic sugar until a real need appears.

#include <cassert>
#include <optional>
#include <utility>
#include <variant>

namespace velocity {

template <typename E>
struct Unexpected {
    E error;
};

template <typename E>
Unexpected<std::decay_t<E>> makeUnexpected(E&& e) {
    return {std::forward<E>(e)};
}

template <typename T, typename E>
class Expected {
public:
    Expected(T value) : v_(std::in_place_index<0>, std::move(value)) {}
    Expected(Unexpected<E> u) : v_(std::in_place_index<1>, std::move(u.error)) {}

    [[nodiscard]] bool hasValue() const { return v_.index() == 0; }
    explicit operator bool() const { return hasValue(); }

    T& value() {
        assert(hasValue());
        return std::get<0>(v_);
    }
    const T& value() const {
        assert(hasValue());
        return std::get<0>(v_);
    }
    E& error() {
        assert(!hasValue());
        return std::get<1>(v_);
    }
    const E& error() const {
        assert(!hasValue());
        return std::get<1>(v_);
    }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() { return value(); }
    const T& operator*() const { return value(); }

private:
    std::variant<T, E> v_;
};

template <typename E>
class Expected<void, E> {
public:
    Expected() = default;
    Expected(Unexpected<E> u) : e_(std::in_place, std::move(u.error)), ok_(false) {}

    [[nodiscard]] bool hasValue() const { return ok_; }
    explicit operator bool() const { return ok_; }
    E& error() {
        assert(!ok_);
        return *e_;
    }
    const E& error() const {
        assert(!ok_);
        return *e_;
    }

private:
    std::optional<E> e_;
    bool ok_ = true;
};

} // namespace velocity
