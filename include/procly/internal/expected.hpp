#pragma once

#include <new>
#include <type_traits>
#include <utility>

namespace procly {

template <typename E>
class unexpected {
 public:
  explicit unexpected(const E& error) : error_(error) {}
  explicit unexpected(E&& error) : error_(std::move(error)) {}

  [[nodiscard]] const E& error() const& noexcept { return error_; }
  [[nodiscard]] E& error() & noexcept { return error_; }
  [[nodiscard]] E&& error() && noexcept { return std::move(error_); }

 private:
  E error_;
};

template <typename E>
unexpected(E) -> unexpected<E>;

namespace detail {

template <typename T, typename E>
struct expected_storage {
  using storage_t = std::aligned_storage_t<(sizeof(T) > sizeof(E) ? sizeof(T) : sizeof(E)),
                                           (alignof(T) > alignof(E) ? alignof(T) : alignof(E))>;

  [[nodiscard]] void* storage_ptr() noexcept { return &storage; }
  [[nodiscard]] const void* storage_ptr() const noexcept { return &storage; }

  [[nodiscard]] T* value_ptr() noexcept { return std::launder(static_cast<T*>(storage_ptr())); }
  [[nodiscard]] const T* value_ptr() const noexcept {
    return std::launder(static_cast<const T*>(storage_ptr()));
  }
  [[nodiscard]] E* error_ptr() noexcept { return std::launder(static_cast<E*>(storage_ptr())); }
  [[nodiscard]] const E* error_ptr() const noexcept {
    return std::launder(static_cast<const E*>(storage_ptr()));
  }

  storage_t storage;
};

}  // namespace detail

template <typename T, typename E>
class expected {
 public:
  using value_type = T;
  using error_type = E;

  expected(const T& value) : has_value_(true) { new (storage_.value_ptr()) T(value); }
  expected(T&& value) : has_value_(true) { new (storage_.value_ptr()) T(std::move(value)); }

  expected(const unexpected<E>& err) { new (storage_.error_ptr()) E(err.error()); }
  expected(unexpected<E>&& err) { new (storage_.error_ptr()) E(std::move(err.error())); }

  expected(const E& err) { new (storage_.error_ptr()) E(err); }
  expected(E&& err) { new (storage_.error_ptr()) E(std::move(err)); }

  expected(const expected& other) : has_value_(other.has_value_) {
    if (has_value_) {
      new (storage_.value_ptr()) T(*other.storage_.value_ptr());
    } else {
      new (storage_.error_ptr()) E(*other.storage_.error_ptr());
    }
  }

  expected(expected&& other) noexcept(
      std::is_nothrow_move_constructible_v<T>&& std::is_nothrow_move_constructible_v<E>)
      : has_value_(other.has_value_) {
    if (has_value_) {
      new (storage_.value_ptr()) T(std::move(*other.storage_.value_ptr()));
    } else {
      new (storage_.error_ptr()) E(std::move(*other.storage_.error_ptr()));
    }
  }

  expected& operator=(const expected& other) {
    if (this == &other) {
      return *this;
    }
    reset();
    has_value_ = other.has_value_;
    if (has_value_) {
      new (storage_.value_ptr()) T(*other.storage_.value_ptr());
    } else {
      new (storage_.error_ptr()) E(*other.storage_.error_ptr());
    }
    return *this;
  }

  expected& operator=(expected&& other) noexcept(
      std::is_nothrow_move_constructible_v<T>&& std::is_nothrow_move_constructible_v<E>) {
    if (this == &other) {
      return *this;
    }
    reset();
    has_value_ = other.has_value_;
    if (has_value_) {
      new (storage_.value_ptr()) T(std::move(*other.storage_.value_ptr()));
    } else {
      new (storage_.error_ptr()) E(std::move(*other.storage_.error_ptr()));
    }
    return *this;
  }

  ~expected() { reset(); }

  [[nodiscard]] bool has_value() const noexcept { return has_value_; }
  explicit operator bool() const noexcept { return has_value_; }

  [[nodiscard]] T& value() & { return *storage_.value_ptr(); }
  [[nodiscard]] const T& value() const& { return *storage_.value_ptr(); }
  [[nodiscard]] T&& value() && { return std::move(*storage_.value_ptr()); }

  [[nodiscard]] T* operator->() noexcept { return storage_.value_ptr(); }
  [[nodiscard]] const T* operator->() const noexcept { return storage_.value_ptr(); }

  [[nodiscard]] E& error() & { return *storage_.error_ptr(); }
  [[nodiscard]] const E& error() const& { return *storage_.error_ptr(); }
  [[nodiscard]] E&& error() && { return std::move(*storage_.error_ptr()); }

  template <typename U>
  T value_or(U&& fallback) const& {
    return has_value_ ? *storage_.value_ptr() : static_cast<T>(std::forward<U>(fallback));
  }

  template <typename F>
  auto transform(F&& func) const& -> expected<std::invoke_result_t<F, const T&>, E> {
    if (!has_value_) {
      return expected<std::invoke_result_t<F, const T&>, E>(*storage_.error_ptr());
    }
    return expected<std::invoke_result_t<F, const T&>, E>(func(*storage_.value_ptr()));
  }

  template <typename F>
  auto and_then(F&& func) const& -> std::invoke_result_t<F, const T&> {
    if (!has_value_) {
      using Ret = std::invoke_result_t<F, const T&>;
      return Ret(*storage_.error_ptr());
    }
    return func(*storage_.value_ptr());
  }

 private:
  void reset() {
    if (has_value_) {
      storage_.value_ptr()->~T();
    } else {
      storage_.error_ptr()->~E();
    }
  }

  bool has_value_{false};
  detail::expected_storage<T, E> storage_;
};

template <typename E>
class expected<void, E> {
 public:
  using value_type = void;
  using error_type = E;

  expected() : has_value_(true) {}
  expected(const unexpected<E>& err) : error_(err.error()) {}
  expected(unexpected<E>&& err) : error_(std::move(err.error())) {}
  expected(const E& err) : error_(err) {}
  expected(E&& err) : error_(std::move(err)) {}

  [[nodiscard]] bool has_value() const noexcept { return has_value_; }
  explicit operator bool() const noexcept { return has_value_; }

  void value() const {}

  [[nodiscard]] E& error() & { return error_; }
  [[nodiscard]] const E& error() const& { return error_; }
  [[nodiscard]] E&& error() && { return std::move(error_); }

  template <typename F>
  auto and_then(F&& func) const& -> std::invoke_result_t<F> {
    if (!has_value_) {
      using Ret = std::invoke_result_t<F>;
      return Ret(error_);
    }
    return func();
  }

 private:
  bool has_value_{false};
  E error_{};
};

}  // namespace procly
