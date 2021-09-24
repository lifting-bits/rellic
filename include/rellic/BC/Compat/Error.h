/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#pragma once

#include <llvm/Support/ErrorOr.h>

#include <system_error>

#include "rellic/BC/Version.h"

#if LLVM_VERSION_NUMBER >= LLVM_VERSION(3, 9)
#include <llvm/Support/Error.h>
#endif

namespace rellic {

template <typename T>
inline static bool IsError(llvm::ErrorOr<T> &val) {
  return !val;
}

#if LLVM_VERSION_NUMBER >= LLVM_VERSION(3, 9)
template <typename T>
inline static bool IsError(llvm::Expected<T> &val) {
  return !val;
}
#endif

template <typename T>
inline static bool IsError(T *ptr) {
  return nullptr == ptr;
}

template <typename T>
inline static bool IsError(T &ptr) {
  return false;
}

template <typename T>
inline static std::string GetErrorString(llvm::ErrorOr<T> &val) {
  return val.getError().message();
}

#if LLVM_VERSION_NUMBER >= LLVM_VERSION(3, 9)
template <typename T>
inline static std::string GetErrorString(llvm::Expected<T> &val) {
  auto err = val.takeError();
  return llvm::errorToErrorCode(std::move(err)).message();
}
#endif

inline static std::string GetErrorString(const char *message) {
  return message;
}

inline static std::string GetErrorString(const std::string *message) {
  return message ? *message : "";
}

inline static std::string GetErrorString(const std::string &message) {
  return message;
}

template <typename T>
inline static std::string GetErrorString(T *) {
  return "";
}

template <typename T>
inline static T *GetPointer(llvm::ErrorOr<T> &val) {
  return val.operator->();
}

#if LLVM_VERSION_NUMBER >= LLVM_VERSION(3, 9)
template <typename T>
inline static T *GetPointer(llvm::Expected<T> &val) {
  return val.operator->();
}
#endif

template <typename T>
inline static T *GetPointer(T *val) {
  return val;
}

template <typename T>
inline static T *GetPointer(T &val) {
  return &val;
}

template <typename T>
inline static T &GetReference(llvm::ErrorOr<T> &val) {
  return val.operator*();
}

#if LLVM_VERSION_NUMBER >= LLVM_VERSION(3, 9)
template <typename T>
inline static T &GetReference(llvm::Expected<T> &val) {
  return val.operator*();
}
#endif

template <typename T>
inline static T &GetReference(T *val) {
  return *val;
}

template <typename T>
inline static T &GetReference(T &val) {
  return val;
}

}  // namespace rellic
