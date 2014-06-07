/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

namespace facebook { namespace memcache {

/**
 * Operation traits allow grouping operations with similar semantics. This way
 * we can write code that deals with for example get, lease-get or metaget in a
 * similar way only once.
 *
 * For example the following method will exist only if Operation is get,
 * lease-get or metaget.
 *
 * template <typename Operation>
 * Result method(..., Operation, GetLike<Operation>::Type = 0)
 */

/**
 * @class GetLike
 * @tparam Operation operation type
 * @brief Utility class to check if operation type is get-like (get, metaget or
 *        lease-get)
 *
 * Boolean 'value' field will be true if and only if operation is get-like
 * Public member typedef 'Type' equal to void* will exist if and only if
 * operation is get-like.
 */
template <typename Operation = void>
struct GetLike {
  static const bool value = false;
};

/**
 * @class UpdateLike
 * @tparam Operation operation type
 * @brief Utility class to check if operation type is update-like (set, add,
 *        replace or lease-set)
 *
 * Boolean 'value' field will be true if and only if operation is update-like
 * Public member typedef 'Type' equal to void* will exist if and only if
 * operation is update-like.
 */
template <typename Operation = void>
struct UpdateLike {
  static const bool value = false;
};

/**
 * @class DeleteLike
 * @tparam Operation operation type
 * @brief Utility class to check if operation type is delete-like (delete)
 *
 * Boolean 'value' field will be true if and only if operation is delete-like
 * Public member typedef 'Type' equal to void* will exist if and only if
 * operation is delete-like.
 */
template <typename Operation = void>
struct DeleteLike {
  static const bool value = false;
};

/**
 * @class ArithmeticLike
 * @tparam Operation operation type
 * @brief Utility class to check if operation type is arithmetic-like (incr,
 *        decr)
 *
 * Boolean 'value' field will be true if and only if operation is
 * arithmetic-like
 * Public member typedef 'Type' equal to void* will exist if and only if
 * operation is arithmetic-like.
 */
template <typename Operation = void>
struct ArithmeticLike {
  static const bool value = false;
};

namespace detail {
/**
 * Hack to make OtherThan compatible with gcc 4.6
 * See http://stackoverflow.com/questions/11297376/gcc-4-6-and-missing-variadic-templates-expansions
 */
template<template <typename...> class T, typename... Args>
struct Join
{
  typedef T<Args...> type;
};

}

/**
 * @class OtherThan
 * @tparam Operation operation type
 * @tparam OperationTraitOrType list of operation types/traits
 * @brief Utility class to check if operator does not belong to any of the
 *        categories/types
 *
 * Boolean 'value' field will be true if and only if operation is not matched
 * by any of the listed traits and is different from all listed operations.
 */
template <typename Operation, typename OperationTraitOrType, typename... Rest>
struct OtherThan {
#ifdef __clang__
  static const bool value =
    OtherThan<Operation, OperationTraitOrType>::value &&
    OtherThan<Operation, Rest...>::value;
#else
  static const bool value =
    OtherThan<Operation, OperationTraitOrType>::value &&
    detail::Join<OtherThan, Operation, Rest...>::type::value;
#endif
};

template <typename Operation, typename OperationTraitOrType>
struct OtherThan<Operation, OperationTraitOrType> {
  static const bool value =
    !std::is_same<Operation, OperationTraitOrType>::value;
};

template <typename Operation>
struct OtherThan<Operation, GetLike<>> {
  static const bool value = !GetLike<Operation>::value;
};
template <typename Operation>
struct OtherThan<Operation, UpdateLike<>> {
  static const bool value = !UpdateLike<Operation>::value;
};
template <typename Operation>
struct OtherThan<Operation, DeleteLike<>> {
  static const bool value = !DeleteLike<Operation>::value;
};
template <typename Operation>
struct OtherThan<Operation, ArithmeticLike<>> {
  static const bool value = !ArithmeticLike<Operation>::value;
};

// This should be type alias in GCC >= 4.7
#define OtherThanT(_Op,...) typename std::enable_if<OtherThan<_Op, __VA_ARGS__>::value, void*>::type

}}
