/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>

#include "mcrouter/lib/fbi/cpp/TypeList.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"

namespace facebook { namespace memcache {

/**
 * We need the wrapper class below since we can't have templated
 * virtual methods.
 *
 * To create a route handle for some route R, use
 *   auto rh = RouteHandle<R>(args);
 */

template<typename Route, typename RouteHandleIf, typename RequestList>
class RouteHandle;

template<typename Route, typename RouteHandleIf>
class RouteHandle<Route, RouteHandleIf, List<>> : public RouteHandleIf {
 public:
  template<typename... Args>
  explicit RouteHandle(Args&&... args)
    : route_(std::forward<Args>(args)...) {}

  std::string routeName() const {
    return route_.routeName();
  }

 protected:
  Route route_;
};

template<typename Route,
         typename RouteHandleIf,
         typename Request,
         typename... Requests>
class RouteHandle<Route, RouteHandleIf, List<Request, Requests...>> :
      public RouteHandle<Route, RouteHandleIf, List<Requests...>> {

 public:
  template<typename... Args>
  explicit RouteHandle(Args&&... args)
    : RouteHandle<Route,
                  RouteHandleIf,
                  List<Requests...>>(std::forward<Args>(args)...) {}

  using RouteHandle<Route, RouteHandleIf, List<Requests...>>::traverse;
  using RouteHandle<Route, RouteHandleIf, List<Requests...>>::route;

  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    this->route_.traverse(req, t);
  }

  ReplyT<Request> route(const Request& req) {
    return this->route_.route(req);
  }
};

template <typename RouteHandleIf_, typename RequestList>
class RouteHandleIf;

template <typename RouteHandleIf_, typename Request>
class RouteHandleIf<RouteHandleIf_, List<Request>> {
 public:
  template <class Route>
  using Impl = RouteHandle<Route, RouteHandleIf_, List<Request>>;

  /**
   * Returns a string identifying this route handle instance (for debugging)
   */
  virtual std::string routeName() const = 0;

  /**
   * Traverses over route handles this route handle could
   * send a request to
   */
  virtual void
  traverse(const Request& req,
           const RouteHandleTraverser<RouteHandleIf_>& t) const = 0;

  /**
   * Routes the request through this route handle
   */
  virtual ReplyT<Request> route(const Request& req) = 0;

  virtual ~RouteHandleIf() {}
};

template <typename RouteHandleIf_, typename Request, typename... Requests>
class RouteHandleIf<RouteHandleIf_, List<Request, Requests...>> :
      public RouteHandleIf<RouteHandleIf_, List<Requests...>> {

 public:
  template <class Route>
  using Impl = RouteHandle<Route, RouteHandleIf_, List<Request, Requests...>>;

  using RouteHandleIf<RouteHandleIf_, List<Requests...>>::traverse;
  using RouteHandleIf<RouteHandleIf_, List<Requests...>>::route;

  /**
   * Returns a string identifying this route handle instance (for debugging)
   */
  virtual std::string routeName() const = 0;

  /**
   * Traverses over route handles this route handle could
   * send a request to
   */
  virtual void
  traverse(const Request& req,
           const RouteHandleTraverser<RouteHandleIf_>& t) const = 0;

  /**
   * Routes the request through this route handle
   */
  virtual ReplyT<Request> route(const Request& req) = 0;

  virtual ~RouteHandleIf() {}
};

}}  // facebook::memcache
