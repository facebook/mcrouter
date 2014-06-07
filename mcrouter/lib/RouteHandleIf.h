/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/lib/fbi/cpp/TypeList.h"

namespace facebook { namespace memcache {

template <typename Operation_, typename Request_>
struct RouteOperation {
  typedef Operation_ Operation;
  typedef Request_ Request;
};

template <typename OperationList, typename RequestList>
struct RouteOperationList;

template <typename OperationList, typename RequestList>
using RouteOperationListT =
  typename RouteOperationList<OperationList, RequestList>::type;

template <typename Operation>
struct RouteOperationList<List<Operation>, List<>> {
  typedef List<> type;
};

template <typename Operation, typename Request, typename... Requests>
struct RouteOperationList<List<Operation>, List<Request, Requests...>> {
  typedef ConcatListsT<
    RouteOperationListT<List<Operation>, List<Requests...>>,
    List<RouteOperation<Operation, Request>>> type;
};

template <typename Operation, typename... Operations, typename... Requests>
struct RouteOperationList<List<Operation, Operations...>, List<Requests...>> {
  typedef ConcatListsT<
    RouteOperationListT<List<Operation>, List<Requests...>>,
    RouteOperationListT<List<Operations...>, List<Requests...>>> type;
};

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

template<typename Route, typename RouteHandleIf, typename List>
class RouteHandle {};

template<typename Route, typename RouteHandleIf>
class RouteHandle<Route, RouteHandleIf, List<>> : public RouteHandleIf {
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
         typename RouteOperation,
         typename... RouteOperations>
class RouteHandle<Route,
                  RouteHandleIf,
                  List<RouteOperation, RouteOperations...>> :
      public RouteHandle<Route,
                         RouteHandleIf,
                         List<RouteOperations...>>{
 public:
  template<typename... Args>
  explicit RouteHandle(Args&&... args)
    : RouteHandle<Route,
                  RouteHandleIf,
                  List<RouteOperations...>>(std::forward<Args>(args)...) {
  }

  using RouteHandle<Route,
                    RouteHandleIf,
                    List<RouteOperations...>>::couldRouteTo;
  using RouteHandle<Route,
                    RouteHandleIf,
                    List<RouteOperations...>>::route;

  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const typename RouteOperation::Request& req,
    typename RouteOperation::Operation) const {

    return this->route_.couldRouteTo(req, typename RouteOperation::Operation());
  }

  typename ReplyType<typename RouteOperation::Operation,
                     typename RouteOperation::Request>::type
  route(const typename RouteOperation::Request& req,
        typename RouteOperation::Operation) {
    return this->route_.route(req, typename RouteOperation::Operation());
  }
};

template <typename RouteHandleIf_, typename List>
class RouteHandleIf;

template <typename RouteHandleIf_, typename RouteOperation>
class RouteHandleIf<RouteHandleIf_, List<RouteOperation>> {
 public:
  template <class Route>
  using Impl = RouteHandle<Route, RouteHandleIf_, List<RouteOperation>>;

  /**
   * Returns a string identifying this route handle instance (for debugging)
   */
  virtual std::string routeName() const = 0;

  /**
   * Returns a list of all possible route handles this route handle could
   * send a request to (for debugging)
   */
  virtual std::vector<std::shared_ptr<RouteHandleIf_>> couldRouteTo(
    const typename RouteOperation::Request& req,
    const typename RouteOperation::Operation) const = 0;

  /**
   * Routes the request through this route handle
   */
  virtual
  typename ReplyType<typename RouteOperation::Operation,
                     typename RouteOperation::Request>::type
  route(const typename RouteOperation::Request& req,
        const typename RouteOperation::Operation) = 0;

  virtual ~RouteHandleIf() {}
};

template <typename RouteHandleIf_,
          typename RouteOperation,
          typename... RouteOperations>
class RouteHandleIf<RouteHandleIf_,
                    List<RouteOperation,
                         RouteOperations...>> :
      public RouteHandleIf<RouteHandleIf_, List<RouteOperations...>> {

 public:
  template <class Route>
  using Impl = RouteHandle<Route, RouteHandleIf_, List<RouteOperation,
                                                       RouteOperations...>>;

  using RouteHandleIf<RouteHandleIf_,
                      List<RouteOperations...>>::couldRouteTo;
  using RouteHandleIf<RouteHandleIf_,
                      List<RouteOperations...>>::route;

  /**
   * Returns a list of all possible route handles this route handle could
   * send a request to (for debugging)
   */
  virtual std::vector<std::shared_ptr<RouteHandleIf_>> couldRouteTo(
    const typename RouteOperation::Request& req,
    const typename RouteOperation::Operation) const = 0;

  /**
   * Routes the request through this route handle
   */
  virtual
  typename ReplyType<typename RouteOperation::Operation,
                     typename RouteOperation::Request>::type
  route(const typename RouteOperation::Request& req,
        const typename RouteOperation::Operation) = 0;
};

}}
