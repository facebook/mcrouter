/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Optional.h>
#include <folly/io/IOBuf.h>
#include <thrift/lib/cpp2/FieldRef.h>
#include <optional>
#include <type_traits>

namespace facebook {
namespace memcache {

namespace detail {
// flags
template <typename Message>
void setFlagsImpl(std::true_type, Message& message, uint64_t flags) {
  message.flags_ref() = flags;
}
template <typename Message>
void setFlagsImpl(std::false_type, Message&, uint64_t) {}
template <typename Message>
uint64_t getFlagsImpl(std::true_type, Message& message) {
  return *message.flags_ref();
}
template <typename Message>
uint64_t getFlagsImpl(std::false_type, Message&) {
  return 0;
}
// exptime
template <class Message>
void setExptimeImpl(std::true_type, Message& message, int32_t exptime) {
  message.exptime_ref() = exptime;
}
template <class Message>
void setExptimeImpl(std::false_type, Message&, int32_t) {}
template <class Message>
int32_t getExptimeImpl(std::true_type, Message& message) {
  return *message.exptime_ref();
}
template <class Message>
int32_t getExptimeImpl(std::false_type, Message&) {
  return 0;
}
// value
template <class Message>
void setValueImpl(std::true_type, Message& message, folly::IOBuf&& buf) {
  message.value_ref() = std::move(buf);
}
template <class Message>
void setValueImpl(std::false_type, Message&, folly::IOBuf&&) {}

template <typename Message>
std::optional<int64_t> getProductIdImpl(std::true_type, Message& message) {
  if (message.productId_ref()) {
    return *message.productId_ref();
  }
  return std::nullopt;
}
template <typename Message>
std::optional<int64_t> getProductIdImpl(std::false_type, Message&) {
  return std::nullopt;
}

template <typename Message>
std::optional<int64_t> getRegionalizationEntityImpl(
    std::true_type,
    Message& message) {
  if (message.regionalizationEntity_ref()) {
    return *message.regionalizationEntity_ref();
  }
  return std::nullopt;
}
template <typename Message>
std::optional<int64_t> getRegionalizationEntityImpl(std::false_type, Message&) {
  return std::nullopt;
}

template <typename Message>
uint64_t getQueryTagsImpl(std::true_type, Message& message) {
  return *message.queryTags_ref();
}
template <typename Message>
uint64_t getQueryTagsImpl(std::false_type, Message&) {
  return 0;
}
} // namespace detail

// // ReadConditions helpers
template <class Message, typename = std::enable_if_t<true>>
struct HasReadConditionsTrait : public std::false_type {};

template <class Message>
struct HasReadConditionsTrait<
    Message,
    std::void_t<decltype(std::declval<Message>().readConditions_ref())>>
    : public std::true_type {};

// Flags helpers
template <typename Message, typename = std::enable_if_t<true>>
class HasFlagsTrait : public std::false_type {};
template <typename Message>
class HasFlagsTrait<
    Message,
    std::enable_if_t<std::is_same<
        decltype(std::declval<std::remove_const_t<Message>&>().flags_ref()),
        apache::thrift::field_ref<std::uint64_t&>>{}>> : public std::true_type {
};
template <class Message>
void setFlags(Message& message, uint64_t flags) {
  detail::setFlagsImpl(HasFlagsTrait<Message>{}, message, flags);
}
// Return flags if it exists in Message and 0 otherwise.
template <class Message>
uint64_t getFlagsIfExist(Message& message) {
  return detail::getFlagsImpl(HasFlagsTrait<Message>{}, message);
}

// LeaseToken helpers
template <typename Message, typename = std::enable_if_t<true>>
class HasLeaseTokenTrait : public std::false_type {};
template <typename Message>
class HasLeaseTokenTrait<
    Message,
    std::enable_if_t<std::is_same<
        decltype(std::declval<std::remove_const_t<Message>&>()
                     .leaseToken_ref()),
        apache::thrift::field_ref<std::int64_t&>>{}>> : public std::true_type {
};
// Return leaseToken if it exists in Message and std::nullopt otherwise.
// Carried by McLeaseGetReply and McLeaseSetRequest only.
template <class Message>
std::optional<int64_t> getLeaseTokenIfExist(Message& message) {
  if constexpr (HasLeaseTokenTrait<Message>::value) {
    return *message.leaseToken_ref();
  }
  return std::nullopt;
}

// Exptime helpers
template <typename Message, typename = std::enable_if_t<true>>
class HasExptimeTrait : public std::false_type {};
template <typename Message>
class HasExptimeTrait<
    Message,
    std::enable_if_t<std::is_same<
        decltype(std::declval<std::remove_const_t<Message>&>().exptime_ref()),
        apache::thrift::field_ref<std::int32_t&>>{}>> : public std::true_type {
};
template <class Message>
void setExptime(Message& message, int32_t exptime) {
  detail::setExptimeImpl(HasExptimeTrait<Message>{}, message, exptime);
}
// Return exptime if it exists in Message and 0 otherwise.
template <class Message>
int32_t getExptimeIfExist(Message& message) {
  return detail::getExptimeImpl(HasExptimeTrait<Message>{}, message);
}

// Value helpers
template <typename Message, typename = std::enable_if_t<true>>
class HasValueTrait : public std::false_type {};
template <typename Message>
class HasValueTrait<
    Message,
    std::enable_if_t<std::is_same<
        decltype(std::declval<std::remove_const_t<Message>&>().value_ref()),
        apache::thrift::field_ref<folly::IOBuf&>>{}>> : public std::true_type {
};
template <typename Message>
class HasValueTrait<
    Message,
    std::enable_if_t<std::is_same<
        decltype(std::declval<std::remove_const_t<Message>&>().value()),
        folly::Optional<folly::IOBuf>&>{}>> : public std::true_type {};
template <typename Message>
class HasValueTrait<
    Message,
    std::enable_if_t<std::is_same<
        decltype(std::declval<std::remove_const_t<Message>&>().value_ref()),
        apache::thrift::optional_field_ref<folly::IOBuf&>>{}>>
    : public std::true_type {};
template <class Message>
void setValue(Message& message, folly::IOBuf&& buf) {
  detail::setValueImpl(
      HasValueTrait<Message>{}, message, std::forward<folly::IOBuf>(buf));
}

// Key helpers
template <typename Message, typename = std::enable_if_t<true>>
class HasKeyTrait : public std::false_type {};
template <typename Message>
class HasKeyTrait<
    Message,
    std::void_t<decltype(std::declval<std::decay_t<Message>&>().key_ref())>>
    : public std::true_type {};

// toFollyOptional helpers
template <class T>
folly::Optional<std::remove_const_t<T>> toFollyOptional(
    apache::thrift::optional_field_ref<T&> ref) {
  if (ref.has_value()) {
    return folly::Optional<std::remove_const_t<T>>(ref.value());
  }
  return folly::none;
}

template <typename Message, typename = std::enable_if_t<true>>
class HasBucketIdTrait : public std::false_type {};
template <typename Message>
class HasBucketIdTrait<
    Message,
    std::void_t<
        decltype(std::declval<std::decay_t<Message>&>().bucketId_ref())>>
    : public std::true_type {};

template <typename Message, typename = std::enable_if_t<true>>
class HasProductIdTrait : public std::false_type {};
template <typename Message>
class HasProductIdTrait<
    Message,
    std::void_t<
        decltype(std::declval<std::decay_t<Message>&>().productId_ref())>>
    : public std::true_type {};

template <typename Message>
std::optional<int64_t> getProductId(Message& message) {
  return detail::getProductIdImpl(HasProductIdTrait<Message>{}, message);
}

template <typename Message, typename = std::enable_if_t<true>>
class HasUsecaseIdTrait : public std::false_type {};
template <typename Message>
class HasUsecaseIdTrait<
    Message,
    std::void_t<decltype(std::declval<Message>().usecaseId_ref())>>
    : public std::true_type {};

template <typename Message, typename = std::enable_if_t<true>>
class HasTenantIdTrait : public std::false_type {};
template <typename Message>
class HasTenantIdTrait<
    Message,
    std::void_t<decltype(std::declval<Message>().tenantId())>>
    : public std::true_type {};

template <typename Message, typename = std::enable_if_t<true>>
class HasFbIdTrait : public std::false_type {};
template <typename Message>
class HasFbIdTrait<
    Message,
    std::void_t<decltype(std::declval<std::decay_t<Message>&>().fbid_ref())>>
    : public std::true_type {};

template <typename Message, typename = std::enable_if_t<true>>
class HasAssocId1Trait : public std::false_type {};
template <typename Message>
class HasAssocId1Trait<
    Message,
    std::void_t<decltype(std::declval<std::decay_t<Message>&>().id1_ref())>>
    : public std::true_type {};

template <typename Message, typename = std::enable_if_t<true>>
class HasMcTenantIdTrait : public std::false_type {};
template <typename Message>
class HasMcTenantIdTrait<
    Message,
    std::void_t<decltype(std::declval<Message>().mcTenantId_ref())>>
    : public std::true_type {};

template <typename Message>
std::optional<int64_t> getUsecaseIdForLogging(Message& message) {
  if constexpr (HasUsecaseIdTrait<Message>::value) {
    if (message.usecaseId_ref().has_value()) {
      return {*message.usecaseId_ref()};
    }
    return std::nullopt;
  } else if constexpr (HasMcTenantIdTrait<Message>::value) {
    if (message.mcTenantId_ref().has_value()) {
      return {*message.mcTenantId_ref()};
    }
    return std::nullopt;
  } else if constexpr (HasTenantIdTrait<Message>::value) {
    // Temporary helper while we are migrating to the new tenantId field.
    // If sent from a new client that sets the tenantId field always use that.
    if (*message.tenantId() > 0) {
      return *message.tenantId();
    }
    auto id = static_cast<uint32_t>(*message.usecaseId_deprecated());
    if (id > 0) {
      return id;
    }
    return std::nullopt;
  } else {
    return std::nullopt;
  }
}

template <typename Message>
uint32_t getTenantId(Message& message) {
  if constexpr (HasMcTenantIdTrait<Message>::value) {
    if (message.mcTenantId().has_value()) {
      return *message.mcTenantId();
    }
    return 0;
  } else {
    static_assert(HasTenantIdTrait<Message>::value);
    // Temporary helper while we are migrating to the new tenantId field.
    // If sent from a new client that sets the tenantId field always use that.
    if (*message.tenantId() > 0) {
      return *message.tenantId();
    }
    return static_cast<uint32_t>(*message.usecaseId_deprecated());
  }
}

template <typename Message>
std::optional<uint64_t> getFbId(const Message& message) {
  if constexpr (HasFbIdTrait<Message>::value) {
    return *message.fbid_ref();
  }
  return std::nullopt;
}

template <typename Message>
std::optional<uint64_t> getAssocId1(const Message& message) {
  if constexpr (HasAssocId1Trait<Message>::value) {
    return *message.id1();
  }
  return std::nullopt;
}

template <typename Message, typename = std::enable_if_t<true>>
class HasRegionalizationEntityTrait : public std::false_type {};
template <typename Message>
class HasRegionalizationEntityTrait<
    Message,
    std::void_t<decltype(std::declval<std::decay_t<Message>&>()
                             .regionalizationEntity_ref())>>
    : public std::true_type {};

template <typename Message>
std::optional<int64_t> getRegionalizationEntity(Message& message) {
  return detail::getRegionalizationEntityImpl(
      HasRegionalizationEntityTrait<Message>{}, message);
}

template <typename Message, typename = std::enable_if_t<true>>
class HasQueryTagsTrait : public std::false_type {};
template <typename Message>
class HasQueryTagsTrait<
    Message,
    std::enable_if_t<std::is_same<
        decltype(std::declval<std::remove_const_t<Message>&>().queryTags_ref()),
        apache::thrift::field_ref<std::uint64_t&>>{}>> : public std::true_type {
};

// Return queryTag if it exists in Message and 0 otherwise.
template <class Message>
uint64_t getQueryTagsIfExists(Message& message) {
  return detail::getQueryTagsImpl(HasQueryTagsTrait<Message>{}, message);
}

template <typename Message, typename = std::enable_if_t<true>>
class HasReplySourceBitMaskTrait : public std::false_type {};
template <typename Message>
class HasReplySourceBitMaskTrait<
    Message,
    std::void_t<decltype(std::declval<Message>().replySourceBitMask_ref())>>
    : public std::true_type {};

template <typename Message>
uint32_t getReplySourceBitMask(const Message& message) {
  if constexpr (HasReplySourceBitMaskTrait<Message>::value) {
    if (message.replySourceBitMask_ref().has_value()) {
      return message.replySourceBitMask_ref().value();
    }
  }
  return 0;
}

template <typename Message, typename = std::enable_if_t<true>>
class HasShardTrait : public std::false_type {};
template <typename Message>
class HasShardTrait<
    Message,
    std::void_t<decltype(std::declval<Message>().shard_ref())>>
    : public std::true_type {};

template <typename Message>
std::optional<int64_t> getMessageShardId(const Message& message) {
  if constexpr (HasShardTrait<Message>::value) {
    if (message.shard().has_value()) {
      return *message.shard()->id_ref();
    }
  }
  return std::nullopt;
}

/**
 * Copies every field that a derived sub-request must inherit from the request
 * it was built from.
 *
 * Route handles that construct sub-requests from scratch rather than copying
 * the parent -- BigValueRoute chunks, L1L2SizeSplitRoute's L2 requests, the
 * refill write-back paths -- must call this instead of copying fields one by
 * one. This is the single definition of what a derived request inherits: a
 * newly added field is handled here once, rather than by auditing every
 * construction site.
 *
 * Carries the RequestCommon context via copyRequestContextFrom (trace context,
 * CAT, client identifier, privacy agentic context, KCB identity, source IP,
 * write timestamp, reply-metadata bitmask) plus every MemcacheRequestCommon
 * field the two messages share: mcTenantId, bucketId, productId and
 * regionalizationEntity. Each is trait-guarded, so a message that lacks a
 * field -- ucache requests carry their own tenantId, for instance -- skips it.
 *
 * Deliberate exclusions:
 *  - flags: call sites differ in whether they copy the parent's flags
 *    verbatim, OR in a routing bit, or take them from a reply.
 *  - beforeLatencyUs / afterLatencyUs: injected latency is per-request, so
 *    propagating it to N chunk sub-requests would multiply the delay the
 *    caller asked for.
 *  - ticket: not one field. Memcache's is an optional std::string
 *    consistency-read ticket; ucache's is a tao::proto::TRep, and on
 *    UcMultiFillRequest a *required fill* ticket sourced from the refill
 *    reply. Those are different concepts with different types and
 *    optionality, so ticket stays explicit at the call sites that need it.
 */
template <class From, class To>
void copyInheritedRequestFields(const From& from, To& to) {
  to.copyRequestContextFrom(from);
  if constexpr (
      HasMcTenantIdTrait<From>::value && HasMcTenantIdTrait<To>::value) {
    to.mcTenantId().copy_from(from.mcTenantId());
  }
  if constexpr (HasBucketIdTrait<From>::value && HasBucketIdTrait<To>::value) {
    to.bucketId().copy_from(from.bucketId());
  }
  if constexpr (
      HasProductIdTrait<From>::value && HasProductIdTrait<To>::value) {
    to.productId().copy_from(from.productId());
  }
  if constexpr (
      HasRegionalizationEntityTrait<From>::value &&
      HasRegionalizationEntityTrait<To>::value) {
    to.regionalizationEntity().copy_from(from.regionalizationEntity());
  }
}

} // namespace memcache
} // namespace facebook
