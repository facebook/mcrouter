/*
 *  Copyright (c) 2013-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/tools/mcpiper/StyleAwareStream.h"

namespace facebook {
namespace memcache {

/**
 * Produces Ansi terminal color codes for a StyledString with color.
 */
class AnsiColorCodeEncoder {
 public:
  explicit AnsiColorCodeEncoder(std::ostream& out);

  /**
   * Writes str into the output stream, inserting color codes as appropriate.
   */
  void write(const StyledString& str);

  /**
   * Resets the color state and outputs the string in the default color.
   */
  template <class T>
  void writePlain(const T& t);

  /**
   * Flushes the output stream.
   */
  void flush() const;

 private:
  std::ostream& out_;
  bool isReset_;

  void reset();
};

using AnsiColorCodeStream = StyleAwareStream<AnsiColorCodeEncoder>;
}
} // facebook::memcache

#include "mcrouter/tools/mcpiper/AnsiColorCodeStream-inl.h"
