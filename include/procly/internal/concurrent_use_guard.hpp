#pragma once

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace procly::internal {

class ConcurrentUseGuard {
 public:
  ConcurrentUseGuard() = default;
  ConcurrentUseGuard(const ConcurrentUseGuard& other) noexcept {
#ifndef NDEBUG
    (void)other;
#endif
  }
  ConcurrentUseGuard& operator=(const ConcurrentUseGuard& other) noexcept {
#ifndef NDEBUG
    (void)other;
    in_use_.clear(std::memory_order_release);
#endif
    return *this;
  }
  ConcurrentUseGuard(ConcurrentUseGuard&& other) noexcept {
#ifndef NDEBUG
    (void)other;
#endif
  }
  ConcurrentUseGuard& operator=(ConcurrentUseGuard&& other) noexcept {
#ifndef NDEBUG
    (void)other;
    in_use_.clear(std::memory_order_release);
#endif
    return *this;
  }

  class Scope {
   public:
    Scope(const ConcurrentUseGuard& guard, const char* context) : guard_(guard) {
      guard_.acquire(context);
    }

    ~Scope() { guard_.release(); }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

   private:
    const ConcurrentUseGuard& guard_;
  };

  [[nodiscard]] Scope enter(const char* context) const { return {*this, context}; }

 private:
  void acquire(const char* context) const {
#ifndef NDEBUG
    if (in_use_.test_and_set(std::memory_order_acq_rel)) {
      std::fputs("procly: concurrent use of non-thread-safe object: ", stderr);
      std::fputs(context, stderr);
      std::fputc('\n', stderr);
      std::abort();
    }
#else
    (void)context;
#endif
  }

  void release() const {
#ifndef NDEBUG
    in_use_.clear(std::memory_order_release);
#endif
  }

#ifndef NDEBUG
  mutable std::atomic_flag in_use_ = ATOMIC_FLAG_INIT;
#endif
};

}  // namespace procly::internal
