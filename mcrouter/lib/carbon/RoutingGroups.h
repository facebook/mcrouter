/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

namespace carbon {

/**
 * Routing groups allow grouping requests with similar semantics. This way
 * we can write code that deals with similar operations only once.
 *
 * For example the following method will exist only if Request represents
 * an operation that is in the "get" routing group.
 *
 * template <typename Request>
 * Result method(..., Request, GetLike<Request>::Type = 0)
 */

/**
 * @class GetLike
 * @tparam Request Request type
 * @brief Utility class to check if Request type is get-like.
 *
 * Boolean 'value' field will be true if and only if Request is get-like
 * Public member typedef 'Type' equal to void* will exist if and only if
 * Request is get-like.
 */
template <typename Request = void>
struct GetLike {
  static const bool value = false;
};

template <typename Request = void>
using GetLikeT = typename GetLike<Request>::Type;

/**
 * @class UpdateLike
 * @tparam Request Request type
 * @brief Utility class to check if Request type is update-like
 *
 * Boolean 'value' field will be true if and only if Request is update-like
 * Public member typedef 'Type' equal to void* will exist if and only if
 * Request is update-like.
 */
template <typename Request = void>
struct UpdateLike {
  static const bool value = false;
};

template <typename Request = void>
using UpdateLikeT = typename UpdateLike<Request>::Type;

/**
 * @class DeleteLike
 * @tparam Request Request type
 * @brief Utility class to check if Request type is delete-like
 *
 * Boolean 'value' field will be true if and only if Request is delete-like
 * Public member typedef 'Type' equal to void* will exist if and only if
 * Request is delete-like.
 */
template <typename Request = void>
struct DeleteLike {
  static const bool value = false;
};

template <typename Request = void>
using DeleteLikeT = typename DeleteLike<Request>::Type;

/**
 * @class ArithmeticLike
 * @tparam Request Request type
 * @brief Utility class to check if Request type is arithmetic-like
 *
 * Boolean 'value' field will be true if and only if Request is
 * arithmetic-like
 * Public member typedef 'Type' equal to void* will exist if and only if
 * Request is arithmetic-like.
 */
template <typename Request = void>
struct ArithmeticLike {
  static const bool value = false;
};

template <typename Request = void>
using ArithmeticLikeT = typename ArithmeticLike<Request>::Type;

/**
 * @class OtherThan
 * @tparam Request Request type
 * @tparam RequestTraitOrType list of Request types/traits
 * @brief Utility class to check if Request does not belong to any of the
 *        categories/types
 *
 * Boolean 'value' field will be true if and only if Request is not matched
 * by any of the listed traits and is different from all listed Requests.
 */
template <typename Request, typename RequestTraitOrType, typename... Rest>
struct OtherThan {
  static const bool value = OtherThan<Request, RequestTraitOrType>::value &&
      OtherThan<Request, Rest...>::value;
};

template <typename Request, typename RequestTraitOrType>
struct OtherThan<Request, RequestTraitOrType> {
  static const bool value = !std::is_same<Request, RequestTraitOrType>::value;
};

template <typename Request>
struct OtherThan<Request, GetLike<>> {
  static const bool value = !GetLike<Request>::value;
};
template <typename Request>
struct OtherThan<Request, UpdateLike<>> {
  static const bool value = !UpdateLike<Request>::value;
};
template <typename Request>
struct OtherThan<Request, DeleteLike<>> {
  static const bool value = !DeleteLike<Request>::value;
};
template <typename Request>
struct OtherThan<Request, ArithmeticLike<>> {
  static const bool value = !ArithmeticLike<Request>::value;
};

template <typename Request, typename... RequestTraitOrType>
using OtherThanT = typename std::
    enable_if<OtherThan<Request, RequestTraitOrType...>::value, void*>::type;
} // carbon
