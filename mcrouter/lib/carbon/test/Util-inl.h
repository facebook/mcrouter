/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <sys/uio.h>

#include <cstring>

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/carbon/CarbonProtocolReader.h"
#include "mcrouter/lib/carbon/CarbonProtocolWriter.h"
#include "mcrouter/lib/carbon/CommonSerializationTraits.h"

namespace carbon {
namespace test {
namespace util {

template <class T>
T serializeAndDeserialize(const T& toSerialize, size_t& bytesWritten) {
  // Serialize the request
  CarbonQueueAppenderStorage storage;
  CarbonProtocolWriter writer(storage);
  toSerialize.serialize(writer);

  // Fill the serialized data into an IOBuf
  folly::IOBuf buf(folly::IOBuf::CREATE, 2048);
  auto* curBuf = &buf;
  const auto iovs = storage.getIovecs();
  bytesWritten = 0;
  for (size_t i = 0; i < iovs.second; ++i) {
    const struct iovec* iov = iovs.first + i;
    size_t written = 0;
    while (written < iov->iov_len) {
      const auto bytesToWrite =
          std::min(iov->iov_len - written, curBuf->tailroom());
      std::memcpy(
          curBuf->writableTail(),
          reinterpret_cast<const uint8_t*>(iov->iov_base) + written,
          bytesToWrite);
      curBuf->append(bytesToWrite);
      written += bytesToWrite;
      bytesWritten += written;

      if (written < iov->iov_len) {
        // Append new buffer with enough room for remaining data in this
        // iovec,
        // plus a bit more space for the next iovec's data
        curBuf->appendChain(
            folly::IOBuf::create(iov->iov_len - written + 2048));
        curBuf = curBuf->next();
      }
    }
  }

  // Deserialize the serialized data
  T deserialized;
  CarbonProtocolReader reader{carbon::CarbonCursor(&buf)};
  deserialized.deserialize(reader);

  return deserialized;
}

template <class T>
T serializeAndDeserialize(const T& toSerialize) {
  size_t tmp;
  return serializeAndDeserialize(toSerialize, tmp);
}
} // util
} // test
} // carbon
