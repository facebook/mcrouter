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

#include <memory>

#include "mcrouter/lib/fbi/cpp/TypeList.h"

namespace facebook { namespace memcache {

/**
 * We need the wrapper class below since we can't have templated
 * virtual methods.
 *
 * To create a route handle for some route R, use
 *   auto rh = RouteHandle<R>(args);
 * or
 *   std::unique_ptr<RouteHandleIf> rh(RouteHandle<R>::makeNamed(name, args));
 *
 * A named instance will have a name "route_name:name" instead of "route_name"
 * where "route_name" is the result of R::routeName().
 */

template<typename Route,
         typename RouteHandleIf,
         typename RequestList,
         typename OpList,
         int op_id = OpList::kLastItemId>
class RouteHandle;

template<typename Route,
         typename RouteHandleIf,
         typename OpList,
         int op_id>
class RouteHandle<Route, RouteHandleIf, List<>, OpList, op_id> :
      public RouteHandleIf {
 public:
  template<typename... Args>
  explicit RouteHandle(Args&&... args)
    : route_(std::forward<Args>(args)...) {
  }

  template<typename... Args>
  explicit RouteHandle(std::string n, Args&&... args)
    : name_(std::move(n)), route_(std::forward<Args>(args)...) {
  }

  template<typename... Args>
  explicit RouteHandle(const char* n, Args&&... args)
    : name_(n), route_(std::forward<Args>(args)...) {
  }

  std::string routeName() const {
    auto name = route_.routeName();
    if (name.empty()) {
      name = "unknown";
    }

    return name + (name_.empty() ? "" : ":" + name_);
  }

 protected:
  std::string name_;
  Route route_;
};

template<typename Route,
         typename RouteHandleIf,
         typename Request,
         typename... Requests,
         typename OpList>
class RouteHandle<Route,
                  RouteHandleIf,
                  List<Request, Requests...>,
                  OpList,
                  0> :
      public RouteHandle<Route,
                         RouteHandleIf,
                         List<Requests...>,
                         OpList,
                         OpList::kLastItemId>{
 public:
  template<typename... Args>
  explicit RouteHandle(Args&&... args)
    : RouteHandle<Route,
                  RouteHandleIf,
                  List<Requests...>,
                  OpList,
                  OpList::kLastItemId>(std::forward<Args>(args)...) {
  }

  using RouteHandle<Route,
                    RouteHandleIf,
                    List<Requests...>,
                    OpList,
                    OpList::kLastItemId>::couldRouteTo;
  using RouteHandle<Route,
                    RouteHandleIf,
                    List<Requests...>,
                    OpList,
                    OpList::kLastItemId>::route;
};

template<typename Route,
         typename RouteHandleIf,
         typename Request,
         typename... Requests,
         typename OpList,
         int op_id>
class RouteHandle<Route,
                  RouteHandleIf,
                  List<Request, Requests...>,
                  OpList,
                  op_id> :
      public RouteHandle<Route,
                         RouteHandleIf,
                         List<Request, Requests...>,
                         OpList,
                         op_id-1>{
 public:
  template<typename... Args>
  explicit RouteHandle(Args&&... args)
    : RouteHandle<Route,
                  RouteHandleIf,
                  List<Request, Requests...>,
                  OpList,
                  op_id-1>(std::forward<Args>(args)...) {
  }

  using RouteHandle<Route,
                    RouteHandleIf,
                    List<Request, Requests...>,
                    OpList,
                    op_id-1>::couldRouteTo;
  using RouteHandle<Route,
                    RouteHandleIf,
                    List<Request, Requests...>,
                    OpList,
                    op_id-1>::route;

  std::vector<std::shared_ptr<RouteHandleIf>>
  couldRouteTo(const Request& req,
               typename OpList::template Item<op_id>::op) const {
    return this->route_.couldRouteTo(
      req, typename OpList::template Item<op_id>::op());
  }

  typename ReplyType<typename OpList::template Item<op_id>::op, Request>::type
  route(const Request& req, typename OpList::template Item<op_id>::op) {
    return this->route_.route(req, typename OpList::template Item<op_id>::op());
  }
};

template <typename RouteHandleIf_,
          typename RequestList,
          typename OpList,
          int op_id = OpList::kLastItemId>
class RouteHandleIf;

template <typename RouteHandleIf_,
          typename Request,
          typename OpList>
class RouteHandleIf<RouteHandleIf_, List<Request>, OpList, 1> {
 public:
  template <class Route>
  using Impl = RouteHandle<Route,
                           RouteHandleIf_,
                           List<Request>,
                           OpList, 1>;

  /**
   * Returns a string identifying this route handle instance (for debugging)
   */
  virtual std::string routeName() const = 0;

  /**
   * Returns a list of all possible route handles this route handle could
   * send a request to
   */
  virtual std::vector<std::shared_ptr<RouteHandleIf_>> couldRouteTo(
    const Request& req, typename OpList::template Item<1>::op) const = 0;

  /**
   * Routes the request through this route handle
   */
  virtual typename ReplyType<typename OpList::template Item<1>::op,
                             Request>::type
  route(const Request& req,
        typename OpList::template Item<1>::op) = 0;

  virtual ~RouteHandleIf() {}
};

template <typename RouteHandleIf_,
          typename Request,
          typename... Requests,
          typename OpList>
class RouteHandleIf<RouteHandleIf_,
                    List<Request, Requests...>,
                    OpList,
                    0> :
      public RouteHandleIf<RouteHandleIf_,
                           List<Requests...>,
                           OpList,
                           OpList::kLastItemId> {

 public:
  using RouteHandleIf<RouteHandleIf_,
                      List<Requests...>,
                      OpList,
                      OpList::kLastItemId>::couldRouteTo;
  using RouteHandleIf<RouteHandleIf_,
                      List<Requests...>,
                      OpList,
                      OpList::kLastItemId>::route;
};

template <typename RouteHandleIf_,
          typename Request,
          typename... Requests,
          typename OpList,
          int op_id>
class RouteHandleIf<RouteHandleIf_,
                    List<Request, Requests...>,
                    OpList,
                    op_id> :
      public RouteHandleIf<RouteHandleIf_,
                           List<Request, Requests...>,
                           OpList,
                           op_id-1> {
 public:
  template <class Route>
  using Impl = RouteHandle<Route,
                           RouteHandleIf_,
                           List<Request, Requests...>,
                           OpList,
                           op_id>;

  using RouteHandleIf<RouteHandleIf_,
                      List<Request, Requests...>,
                      OpList,
                      op_id-1>::couldRouteTo;
  using RouteHandleIf<RouteHandleIf_,
                      List<Request, Requests...>,
                      OpList,
                      op_id-1>::route;

  /**
   * Returns a list of all possible route handles this route handle could
   * send a request to
   */
  virtual std::vector<std::shared_ptr<RouteHandleIf_>> couldRouteTo(
    const Request& req, typename OpList::template Item<op_id>::op) const = 0;

  /**
   * Routes the request through this route handle
   */
  virtual typename ReplyType<typename OpList::template Item<op_id>::op,
                             Request>::type
  route(const Request& req, typename OpList::template Item<op_id>::op) = 0;
};

}}  // facebook::memcache
