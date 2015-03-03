/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
namespace facebook { namespace memcache {

struct AsciiSerializedRequest::PrepareImplWrapper {
  template <class Request, class Operation>
  using PrepareType =
    decltype(std::declval<AsciiSerializedRequest>().prepareImpl(
      std::declval<const Request&>(), std::declval<Operation>()));

  template <class Request, class Operation>
  typename std::enable_if<
    std::is_same<PrepareType<Request, Operation>, std::false_type>::value,
    bool>::type
  static prepare(AsciiSerializedRequest& s, const Request& request,
                      Operation) {
    return false;
  }

  template <class Request, class Operation>
  typename std::enable_if<
    std::is_same<PrepareType<Request, Operation>, void>::value,
    bool>::type
  static prepare(AsciiSerializedRequest& s, const Request& request,
                      Operation) {
    s.prepareImpl(request, Operation());
    return true;
  }
};

template <class Arg1, class Arg2>
void AsciiSerializedRequest::addStrings(Arg1&& arg1, Arg2&& arg2) {
  addString(std::forward<Arg1>(arg1));
  addString(std::forward<Arg2>(arg2));
}

template <class Arg, class... Args>
void AsciiSerializedRequest::addStrings(Arg&& arg, Args&&... args) {
  addString(std::forward<Arg>(arg));
  addStrings(std::forward<Args>(args)...);
}

template <class Operation, class Request>
bool AsciiSerializedRequest::prepare(const Request& request, Operation,
                                     struct iovec*& iovOut, size_t& niovOut) {
  iovsCount_ = 0;
  auto r = PrepareImplWrapper::prepare(*this, request, Operation());
  iovOut = iovs_;
  niovOut = iovsCount_;
  return r;
}

}} // facebook::memcache
