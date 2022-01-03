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
#include <string>

namespace rellic {
class Exception : public std::runtime_error {
 public:
  Exception(const std::string& what);
  Exception(const char* what);
};

template <typename T = Exception>
class StreamThrower {
  std::stringstream stream;
  bool triggered;

 public:
  StreamThrower(bool cond = true) : triggered(cond) {}
  ~StreamThrower() noexcept(false) {
    if (triggered) {
      throw T(stream.str());
    }
  }

  template <typename V>
  std::ostream& operator<<(V& s) {
    return stream << s;
  }

  template <typename V>
  std::ostream& operator<<(V&& s) {
    return stream << s;
  }
};
}  // namespace rellic

#define THROW() ::rellic::StreamThrower<::rellic::Exception>(true)
#define THROW_IF(cond) ::rellic::StreamThrower<::rellic::Exception>((cond))
#define CHECK_THROW(cond) THROW_IF(!(cond)) << "Check failed: " #cond " "