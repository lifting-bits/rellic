/*
 * Copyright (c) 2019-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <sstream>
#include <stdexcept>
#include <utility>

namespace rellic {
class Exception : public std::runtime_error {
 public:
  Exception(const std::string& what);
  Exception(const char* what);
};

template <typename T = Exception>
class StreamThrower {
  std::stringstream stream;
  bool triggered, moved = false;

 public:
  StreamThrower(bool cond = true, std::stringstream ss = std::stringstream())
      : stream(std::move(ss)), triggered(cond) {}
  ~StreamThrower() noexcept(false) {
    if (triggered && !moved) {
      throw T(stream.str());
    }
  }

  template <typename V>
  StreamThrower<T> operator<<(V&& s) {
    moved = true;
    stream << std::forward<V>(s);
    return StreamThrower<T>(triggered, std::move(stream));
  }
};
}  // namespace rellic

#define THROW_IF(cond)                                 \
  ::rellic::StreamThrower<::rellic::Exception>((cond)) \
      << __FILE__ << ':' << __LINE__ << ' '
#define THROW() THROW_IF(true)
#define CHECK_THROW(cond) THROW_IF(!(cond)) << "Check failed: " #cond " "