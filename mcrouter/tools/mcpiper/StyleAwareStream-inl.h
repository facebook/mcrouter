/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

namespace facebook { namespace memcache {

template <class Encoder>
StyleAwareStream<Encoder>::StyleAwareStream(std::ostream& out)
    : encoder_(out), useColor_(true) {
}

template <class Encoder>
void StyleAwareStream<Encoder>::setColorOutput(bool useColor) {
  useColor_ = useColor;
}

template <class Encoder>
void StyleAwareStream<Encoder>::writePlain(const folly::StringPiece& sp) {
  encoder_.writePlain(sp);
}

template <class Encoder>
StyleAwareStream<Encoder>&
StyleAwareStream<Encoder>::operator<<(const std::string& s) {
  encoder_.writePlain(s);
  return *this;
}

template <class Encoder>
StyleAwareStream<Encoder>&
StyleAwareStream<Encoder>::operator<<(const StyledString& s) {
  if (useColor_) {
    encoder_.write(s);
  } else {
    encoder_.writePlain(s.text());
  }
  return *this;
}

template <class Encoder>
StyleAwareStream<Encoder>& StyleAwareStream<Encoder>::operator<<(char c) {
  encoder_.writePlain(c);
  return *this;
}

template <class Encoder>
template <bool containerMode, class... Args>
StyleAwareStream<Encoder>& StyleAwareStream<Encoder>::operator<<(
  const folly::Formatter<containerMode, Args...>& formatter) {

  auto writer = [this] (const folly::StringPiece& sp) {
    this->writePlain(sp);
  };
  formatter(writer);
  return *this;
}

template <class Encoder>
void StyleAwareStream<Encoder>::flush() const {
  encoder_.flush();
}

}} // facebook::memcache
