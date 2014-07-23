/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
namespace facebook { namespace memcache {

template <class Ctx>
McRequestWithContext<Ctx> McRequestWithContext<Ctx>::clone() const {
  return McRequestWithContext<Ctx>(*this);
}

template <class Ctx>
McRequestWithContext<Ctx>::McRequestWithContext(std::shared_ptr<Ctx> ctx,
                                                McMsgRef&& msg)
  : McRequestBase(std::move(msg)),
    ctx_(std::move(ctx)) {}

template <class Ctx>
McRequestWithContext<Ctx>::McRequestWithContext(std::shared_ptr<Ctx> ctx,
                                                const std::string& key)
  : McRequestBase(key),
    ctx_(std::move(ctx)) {}

template <class Ctx>
typename std::add_lvalue_reference<Ctx>::type
McRequestWithContext<Ctx>::context() const {
  return *ctx_;
}

template<class Ctx>
std::shared_ptr<Ctx> McRequestWithContext<Ctx>::contextPtr() const {
  return ctx_;
}

template <class Ctx>
McRequestWithContext<Ctx>::McRequestWithContext(
    const McRequestWithContext& other)
  : McRequestBase(other),
    ctx_(other.ctx_) {}

}}
