/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mcrouter/lib/network/SocketUtil.h"

#include <type_traits>

#include <folly/Expected.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/EventBase.h>

#include <thrift/lib/cpp/async/TAsyncSSLSocket.h>
#include <thrift/lib/cpp/async/TAsyncSocket.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/network/AsyncTlsToPlaintextSocket.h"
#include "mcrouter/lib/network/ConnectionOptions.h"
#include "mcrouter/lib/network/McFizzClient.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"

namespace facebook {
namespace memcache {

namespace {

std::string getServiceIdentity(const ConnectionOptions& opts) {
  const auto& svcIdentity = opts.securityOpts.sslServiceIdentity;
  return svcIdentity.empty() ? opts.accessPoint->toHostPortString()
                             : svcIdentity;
}

void createTCPKeepAliveOptions(
    folly::AsyncSocket::OptionMap& options,
    int cnt,
    int idle,
    int interval) {
  // 0 means KeepAlive is disabled.
  if (cnt != 0) {
#ifdef SO_KEEPALIVE
    folly::AsyncSocket::OptionMap::key_type key;
    key.level = SOL_SOCKET;
    key.optname = SO_KEEPALIVE;
    options[key] = 1;

    key.level = IPPROTO_TCP;

#ifdef TCP_KEEPCNT
    key.optname = TCP_KEEPCNT;
    options[key] = cnt;
#endif // TCP_KEEPCNT

#ifdef TCP_KEEPIDLE
    key.optname = TCP_KEEPIDLE;
    options[key] = idle;
#endif // TCP_KEEPIDLE

#ifdef TCP_KEEPINTVL
    key.optname = TCP_KEEPINTVL;
    options[key] = interval;
#endif // TCP_KEEPINTVL

#endif // SO_KEEPALIVE
  }
}

const folly::AsyncSocket::OptionKey getQoSOptionKey(sa_family_t addressFamily) {
  static const folly::AsyncSocket::OptionKey kIpv4OptKey = {IPPROTO_IP, IP_TOS};
  static const folly::AsyncSocket::OptionKey kIpv6OptKey = {IPPROTO_IPV6,
                                                            IPV6_TCLASS};
  return (addressFamily == AF_INET) ? kIpv4OptKey : kIpv6OptKey;
}

uint64_t getQoS(uint64_t qosClassLvl, uint64_t qosPathLvl) {
  // class
  static const uint64_t kDefaultClass = 0x00;
  static const uint64_t kLowestClass = 0x20;
  static const uint64_t kMediumClass = 0x40;
  static const uint64_t kHighClass = 0x60;
  static const uint64_t kHighestClass = 0x80;
  static const uint64_t kQoSClasses[] = {
      kDefaultClass, kLowestClass, kMediumClass, kHighClass, kHighestClass};

  // path
  static const uint64_t kAnyPathNoProtection = 0x00;
  static const uint64_t kAnyPathProtection = 0x04;
  static const uint64_t kShortestPathNoProtection = 0x08;
  static const uint64_t kShortestPathProtection = 0x0c;
  static const uint64_t kQoSPaths[] = {kAnyPathNoProtection,
                                       kAnyPathProtection,
                                       kShortestPathNoProtection,
                                       kShortestPathProtection};

  if (qosClassLvl > 4) {
    qosClassLvl = 0;
    LOG_FAILURE(
        "AsyncMcClient",
        failure::Category::kSystemError,
        "Invalid QoS class value in AsyncMcClient");
  }

  if (qosPathLvl > 3) {
    qosPathLvl = 0;
    LOG_FAILURE(
        "AsyncMcClient",
        failure::Category::kSystemError,
        "Invalid QoS path value in AsyncMcClient");
  }

  return kQoSClasses[qosClassLvl] | kQoSPaths[qosPathLvl];
}

void createQoSClassOption(
    folly::AsyncSocket::OptionMap& options,
    const sa_family_t addressFamily,
    uint64_t qosClass,
    uint64_t qosPath) {
  const auto& optkey = getQoSOptionKey(addressFamily);
  options[optkey] = getQoS(qosClass, qosPath);
}

} // namespace

template <bool CreateThriftFriendlySocket>
folly::Expected<
    std::conditional_t<
        CreateThriftFriendlySocket,
        apache::thrift::async::TAsyncTransport::UniquePtr,
        folly::AsyncTransportWrapper::UniquePtr>,
    folly::AsyncSocketException>
createSocketCommon(
    folly::EventBase& eventBase,
    const ConnectionOptions& connectionOptions) {
  using AsyncSocketT = std::conditional_t<
      CreateThriftFriendlySocket,
      apache::thrift::async::TAsyncSocket,
      folly::AsyncSocket>;
  using AsyncSSLSocketT = std::conditional_t<
      CreateThriftFriendlySocket,
      apache::thrift::async::TAsyncSSLSocket,
      folly::AsyncSSLSocket>;
  using Ptr = std::conditional_t<
      CreateThriftFriendlySocket,
      apache::thrift::async::TAsyncTransport::UniquePtr,
      folly::AsyncTransportWrapper::UniquePtr>;

  Ptr socket;

  const auto mech = connectionOptions.accessPoint->getSecurityMech();
  if (mech == SecurityMech::NONE) {
    socket.reset(new AsyncSocketT(&eventBase));
    return socket;
  }

  // Only AsyncMcClient sockets support Fizz; that creation logic lives outside
  // this helper function.
  DCHECK(isAsyncSSLSocketMech(mech));
  // Creating a secure transport - make sure it isn't over a unix domain sock
  if (connectionOptions.accessPoint->isUnixDomainSocket()) {
    return folly::makeUnexpected(folly::AsyncSocketException(
        folly::AsyncSocketException::BAD_ARGS,
        "SSL protocol is not applicable for Unix Domain Sockets"));
  }
  const auto& securityOpts = connectionOptions.securityOpts;
  const auto& serviceId = getServiceIdentity(connectionOptions);
  // openssl based tls
  auto sslContext = getClientContext(securityOpts, mech);
  if (!sslContext) {
    return folly::makeUnexpected(folly::AsyncSocketException(
        folly::AsyncSocketException::SSL_ERROR,
        "SSLContext provider returned nullptr, "
        "check SSL certificates"));
  }

  typename AsyncSSLSocketT::UniquePtr sslSocket(
      new AsyncSSLSocketT(sslContext, &eventBase));
  if (securityOpts.sessionCachingEnabled) {
    if (auto clientCtx =
            std::dynamic_pointer_cast<ClientSSLContext>(sslContext)) {
      sslSocket->setSessionKey(serviceId);
      auto session = clientCtx->getCache().getSSLSession(serviceId);
      if (session) {
        sslSocket->setSSLSession(session.release(), true);
      }
    }
  }
  if (securityOpts.tfoEnabledForSsl) {
    sslSocket->enableTFO();
  }
  sslSocket->forceCacheAddrOnFailure(true);
  socket.reset(sslSocket.release());
  return socket;
}

folly::Expected<
    folly::AsyncTransportWrapper::UniquePtr,
    folly::AsyncSocketException>
createSocket(
    folly::EventBase& eventBase,
    const ConnectionOptions& connectionOptions) {
  folly::AsyncTransportWrapper::UniquePtr socket;

  const auto mech = connectionOptions.accessPoint->getSecurityMech();
  if (mech == SecurityMech::NONE || isAsyncSSLSocketMech(mech)) {
    return createSocketCommon<false>(eventBase, connectionOptions);
  }

  DCHECK(mech == SecurityMech::TLS13_FIZZ);
  // creating a secure transport - make sure it isn't over a unix domain sock
  if (connectionOptions.accessPoint->isUnixDomainSocket()) {
    return folly::makeUnexpected(folly::AsyncSocketException(
        folly::AsyncSocketException::BAD_ARGS,
        "SSL protocol is not applicable for Unix Domain Sockets"));
  }
  const auto& securityOpts = connectionOptions.securityOpts;
  const auto& serviceId = getServiceIdentity(connectionOptions);
  // tls 13 fizz
  auto fizzContextAndVerifier = getFizzClientConfig(securityOpts);
  if (!fizzContextAndVerifier.first) {
    return folly::makeUnexpected(folly::AsyncSocketException(
        folly::AsyncSocketException::SSL_ERROR,
        "Fizz context provider returned nullptr, "
        "check SSL certificates"));
  }
  auto fizzClient = new McFizzClient(
      &eventBase,
      std::move(fizzContextAndVerifier.first),
      std::move(fizzContextAndVerifier.second));
  fizzClient->setSessionKey(serviceId);
  if (securityOpts.tfoEnabledForSsl) {
    if (auto underlyingSocket =
            fizzClient->getUnderlyingTransport<folly::AsyncSocket>()) {
      underlyingSocket->enableTFO();
    }
  }
  socket.reset(fizzClient);
  return socket;
}

folly::Expected<
    apache::thrift::async::TAsyncTransport::UniquePtr,
    folly::AsyncSocketException>
createTAsyncSocket(
    folly::EventBase& eventBase,
    const ConnectionOptions& connectionOptions) {
  auto socket = createSocketCommon<true>(eventBase, connectionOptions);

  const auto mech = connectionOptions.accessPoint->getSecurityMech();
  if (mech == SecurityMech::NONE || socket.hasError()) {
    return socket;
  }
  DCHECK(mech == SecurityMech::TLS_TO_PLAINTEXT);
  return AsyncTlsToPlaintextSocket::create(std::move(socket).value());
}

folly::Expected<folly::SocketAddress, folly::AsyncSocketException>
getSocketAddress(const ConnectionOptions& connectionOptions) {
  try {
    folly::SocketAddress address;
    if (connectionOptions.accessPoint->isUnixDomainSocket()) {
      address.setFromPath(connectionOptions.accessPoint->getHost());
    } else {
      address = folly::SocketAddress(
          connectionOptions.accessPoint->getHost(),
          connectionOptions.accessPoint->getPort(),
          /* allowNameLookup */ true);
    }
    return address;
  } catch (const std::system_error& e) {
    return folly::makeUnexpected(folly::AsyncSocketException(
        folly::AsyncSocketException::NOT_OPEN,
        folly::sformat(
            "AsyncMcClient",
            failure::Category::kBadEnvironment,
            "{}",
            e.what())));
  }
}

folly::AsyncSocket::OptionMap createSocketOptions(
    const folly::SocketAddress& address,
    const ConnectionOptions& connectionOptions) {
  folly::AsyncSocket::OptionMap options;

  createTCPKeepAliveOptions(
      options,
      connectionOptions.tcpKeepAliveCount,
      connectionOptions.tcpKeepAliveIdle,
      connectionOptions.tcpKeepAliveInterval);
  if (connectionOptions.enableQoS) {
    createQoSClassOption(
        options,
        address.getFamily(),
        connectionOptions.qosClass,
        connectionOptions.qosPath);
  }

  return options;
}

void checkWhetherQoSIsApplied(
    int socketFd,
    const folly::SocketAddress& address,
    const ConnectionOptions& connectionOptions,
    folly::StringPiece transportName) {
  const auto& optkey = getQoSOptionKey(address.getFamily());

  const uint64_t expectedValue =
      getQoS(connectionOptions.qosClass, connectionOptions.qosPath);

  uint64_t val = 0;
  socklen_t len = sizeof(expectedValue);
  int rv = getsockopt(socketFd, optkey.level, optkey.optname, &val, &len);
  // Zero out last 2 bits as they are not used for the QOS value
  constexpr uint64_t kMaskTwoLeastSignificantBits = 0xFFFFFFFc;
  val = val & kMaskTwoLeastSignificantBits;
  if (rv != 0 || val != expectedValue) {
    LOG_FAILURE(
        transportName,
        failure::Category::kSystemError,
        "Failed to apply QoS! "
        "Return Value: {} (expected: {}). "
        "QoS Value: {} (expected: {}).",
        rv,
        0,
        val,
        expectedValue);
  }
}

} // namespace memcache
} // namespace facebook
