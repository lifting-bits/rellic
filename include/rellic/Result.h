/*
 * Copyright (c) 2019-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <glog/logging.h>

#include <new>
#include <stdexcept>
#include <utility>

namespace rellic {

template <typename ValueType, typename ErrorType>
class Result final {
 private:
  static constexpr auto kMaxAlign = alignof(ValueType) > alignof(ErrorType)
                                        ? alignof(ValueType)
                                        : alignof(ErrorType);

  static constexpr auto kMaxSize = sizeof(ValueType) > sizeof(ErrorType)
                                       ? sizeof(ValueType)
                                       : sizeof(ErrorType);

  alignas(kMaxAlign) uint8_t data[kMaxSize];
  ValueType *value{nullptr};
  ErrorType *error{nullptr};
  bool destroyed{true};
  mutable bool checked{false};

 public:
  Result(void);
  ~Result(void) {
    if (value) {
      value->~ValueType();
    } else if (error) {
      error->~ErrorType();
    }
  }

  bool Succeeded(void) const;

  inline bool Failed(void) const { return !Succeeded(); }

  const ErrorType &Error(void) const;
  ErrorType TakeError(void);

  const ValueType &Value(void) const;
  ValueType TakeValue(void);

  const ValueType *operator->(void) const;

  Result(const ValueType &value) noexcept;
  Result(ValueType &&value) noexcept;

  Result(const ErrorType &error) noexcept;
  Result(ErrorType &&error) noexcept;

  Result(Result &&other) noexcept;
  Result &operator=(Result &&other) noexcept;

  Result(const Result &) = delete;
  Result &operator=(const Result &) = delete;

 private:
  void VerifyState(void) const;
  void VerifyChecked(void) const;
  void VerifyFailed(void) const;
  void VerifySucceeded(void) const;
};

template <typename ValueType, typename ErrorType>
Result<ValueType, ErrorType>::Result(void) {
  checked = true;
  error = new (data) ErrorType;
}

template <typename ValueType, typename ErrorType>
bool Result<ValueType, ErrorType>::Succeeded(void) const {
  VerifyState();
  checked = true;
  return value != nullptr;
}

template <typename ValueType, typename ErrorType>
const ErrorType &Result<ValueType, ErrorType>::Error(void) const {
  VerifyState();
  VerifyChecked();
  VerifyFailed();
  return *error;
}

template <typename ValueType, typename ErrorType>
ErrorType Result<ValueType, ErrorType>::TakeError(void) {
  VerifyState();
  VerifyChecked();
  VerifyFailed();
  destroyed = true;
  return std::move(*error);
}

template <typename ValueType, typename ErrorType>
const ValueType &Result<ValueType, ErrorType>::Value(void) const {
  VerifyState();
  VerifyChecked();
  VerifySucceeded();
  return std::move(*value);
}

template <typename ValueType, typename ErrorType>
ValueType Result<ValueType, ErrorType>::TakeValue(void) {
  VerifyState();
  VerifyChecked();
  VerifySucceeded();
  destroyed = true;
  return std::move(*value);
}

template <typename ValueType, typename ErrorType>
const ValueType *Result<ValueType, ErrorType>::operator->(void) const {
  return &Value();
}

template <typename ValueType, typename ErrorType>
Result<ValueType, ErrorType>::Result(const ValueType &value_) noexcept
    : value(new (data) ValueType(value_)) {
  destroyed = false;
}

template <typename ValueType, typename ErrorType>
Result<ValueType, ErrorType>::Result(ValueType &&value_) noexcept
    : value(new (data) ValueType(std::forward<ValueType>(value_))) {
  destroyed = false;
}

template <typename ValueType, typename ErrorType>
Result<ValueType, ErrorType>::Result(const ErrorType &error_) noexcept
    : error(new (data) ErrorType(error_)) {
  destroyed = false;
}

template <typename ValueType, typename ErrorType>
Result<ValueType, ErrorType>::Result(ErrorType &&error_) noexcept
    : error(new (data) ErrorType(std::forward<ErrorType>(error_))) {
  destroyed = false;
}

template <typename ValueType, typename ErrorType>
Result<ValueType, ErrorType>::Result(Result &&other) noexcept {
  if (other.error) {
    error = new (data) ErrorType(*(other.error));
  } else if (other.value) {
    value = new (data) ValueType(*(other.value));
  }
  checked = std::exchange(other.checked, true);
  destroyed = std::exchange(other.destroyed, false);
}

template <typename ValueType, typename ErrorType>
Result<ValueType, ErrorType> &Result<ValueType, ErrorType>::operator=(
    Result &&other) noexcept {
  if (this != &other) {
    if (error) {
      error->~ErrorType();
    } else if (value) {
      value->~ValueType();
    }

    if (other.error) {
      error = new (data) ErrorType(std::move(*(other.error)));
    } else if (other.value) {
      value = new (data) ValueType(std::move(*(other.value)));
    }

    checked = std::exchange(other.checked, true);
    destroyed = std::exchange(other.destroyed, false);
  }

  return *this;
}

template <typename ValueType, typename ErrorType>
void Result<ValueType, ErrorType>::VerifyState(void) const {
  CHECK(!destroyed)
      << "The Result<ValueType, ErrorType> object no longer contains its "
         "internal data because it has been moved with TakeError/TakeValue";
}

template <typename ValueType, typename ErrorType>
void Result<ValueType, ErrorType>::VerifyChecked(void) const {
  CHECK(checked)
      << "The Result<ValueType, ErrorType> object was not checked for success";
}

template <typename ValueType, typename ErrorType>
void Result<ValueType, ErrorType>::VerifySucceeded(void) const {
  CHECK(value != nullptr)
      << "The Result<ValueType, ErrorType> object has not succeeded";
}

template <typename ValueType, typename ErrorType>
void Result<ValueType, ErrorType>::VerifyFailed(void) const {
  CHECK(error != nullptr)
      << "The Result<ValueType, ErrorType> object has not failed";
}

}  // namespace rellic
