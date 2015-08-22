// Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#ifndef SRC_SHARED_ATOMIC_H_
#define SRC_SHARED_ATOMIC_H_

namespace fletch {

enum MemoryOrder {
  kRelaxed = __ATOMIC_RELAXED,
  kConsume = __ATOMIC_CONSUME,
  kAcquire = __ATOMIC_ACQUIRE,
  kRelease = __ATOMIC_RELEASE,
  kAcqRel = __ATOMIC_ACQ_REL,
  kSeqCst = __ATOMIC_SEQ_CST,
};

// TODO(ajohnsen): Put compiler-specific builtins in a seperate header file to
// allow easy port to other compilers.
// Wrapper for working with atomic values. This implementation follows the
// names of the C++11 std::atomic interface, to ease portability.
template<typename T>
class Atomic {
 public:
  Atomic() : value_(T()) { }

  Atomic(T value) : value_(value) { }

  T operator=(T other) {
    store(other);
    return other;
  }

  operator T() const {
    return load();
  }

  T operator++() {
    return add_fetch(1);
  }

  T operator--() {
    return sub_fetch(1);
  }

  T operator++(int) {
    return fetch_add(1);
  }

  T operator--(int) {
    return fetch_sub(1);
  }

  T operator+=(T other) {
    return add_fetch(other);
  }

  T operator-=(T other) {
    return sub_fetch(other);
  }

  void store(T other, MemoryOrder order = kSeqCst) {
    __atomic_store(&value_, &other, order);
  }

  T load(MemoryOrder order = kSeqCst) const {
    T result;
    __atomic_load(&value_, &result, order);
    return result;
  }

  T exchange(T other, MemoryOrder order = kSeqCst) {
    T result;
    __atomic_exchange(&value_, &other, &result, order);
    return result;
  }

  bool compare_exchange_weak(T& expected,
                             T other,
                             MemoryOrder order = kSeqCst) {
    return __atomic_compare_exchange(
        &value_, &expected, &other, true, order, order);
  }

  bool compare_exchange_weak(T& expected,
                             T other,
                             MemoryOrder success,
                             MemoryOrder failure) {
    return __atomic_compare_exchange(
        &value_, &expected, &other, true, success, failure);
  }

  bool compare_exchange_strong(T& expected,
                               T other,
                               MemoryOrder order = kSeqCst) {
    return __atomic_compare_exchange(
        &value_, &expected, &other, false, order, order);
  }

  bool compare_exchange_strong(T& expected,
                               T other,
                               MemoryOrder success,
                               MemoryOrder failure) {
    return __atomic_compare_exchange(
        &value_, &expected, &other, false, success, failure);
  }

  T add_fetch(T other, MemoryOrder order = kSeqCst) {
#ifdef __pnacl__
    T old_value = value_;
    T new_value;
    do {
      new_value = old_value + other;
    } while (!compare_exchange_weak(old_value, new_value));
    return new_value;
#else
    return __atomic_add_fetch(&value_, other, order);
#endif
  }

  T sub_fetch(T other, MemoryOrder order = kSeqCst) {
#ifdef __pnacl__
    T old_value = value_;
    T new_value;
    do {
      new_value = old_value - other;
    } while (!compare_exchange_weak(old_value, new_value));
    return new_value;
#else
    return __atomic_sub_fetch(&value_, other, order);
#endif
  }

  T fetch_add(T other, MemoryOrder order = kSeqCst) {
#ifdef __pnacl__
    T old_value = value_;
    T new_value;
    do {
      new_value = old_value + other;
    } while (!compare_exchange_weak(old_value, new_value));
    return old_value;
#else
    return __atomic_fetch_add(&value_, other, order);
#endif
  }

  T fetch_sub(T other, MemoryOrder order = kSeqCst) {
#ifdef __pnacl__
    T old_value = value_;
    T new_value;
    do {
      new_value = old_value - other;
    } while (!compare_exchange_weak(old_value, new_value));
    return old_value;
#else
    return __atomic_fetch_sub(&value_, other, order);
#endif
  }

 private:
  T value_;
};

}

#endif  // SRC_SHARED_ATOMIC_H_
