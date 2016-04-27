/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <sys/uio.h>

#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <folly/io/IOBuf.h>
#include <thrift/lib/cpp2/protocol/CompactProtocol.h>

#include "mcrouter/lib/network/McQueueAppender.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types_custom_protocol.h"
#include "mcrouter/lib/network/gen-cpp2/caret_test_types.h"
#include "mcrouter/lib/network/gen-cpp2/caret_test_types_custom_protocol.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

using namespace facebook::memcache;

TEST(McQueueAppenderTest, longString) {
  McQueueAppenderStorage storage;
  TypedThriftReply<cpp2::McGetReply> reply(mc_res_remote_error);

  // Require more space than McQueueAppenderStorage's internal 512B buffer.
  // This will append() a copy of the string allocated on the heap.
  const std::string message(1024, 'a');
  reply->set_message(message);

  apache::thrift::CompactProtocolWriterImpl<
      McQueueAppender, McQueueAppenderStorage> writer(
          apache::thrift::SHARE_EXTERNAL_BUFFER);

  writer.setOutput(&storage);
  reply->write(&writer);

  UmbrellaMessageInfo info;
  info.bodySize = storage.computeBodySize();
  info.typeId = 123;
  info.reqId = 456;
  info.traceId = 17;

  size_t headerSize = caretPrepareHeader(
      info, reinterpret_cast<char*>(storage.getHeaderBuf()));
  storage.reportHeaderSize(headerSize);

  folly::IOBuf input(folly::IOBuf::CREATE, 2048);
  const auto iovs = storage.getIovecs();
  for (size_t i = 0; i < iovs.second; ++i) {
    const struct iovec* iov = iovs.first + i;
    std::memcpy(input.writableTail(), iov->iov_base, iov->iov_len);
    input.append(iov->iov_len);
  }

  // Read the serialized data back in and check that it's what we wrote.
  UmbrellaMessageInfo inputHeader;
  caretParseHeader((uint8_t*)input.data(), input.length(), inputHeader);
  EXPECT_EQ(123, inputHeader.typeId);
  EXPECT_EQ(456, inputHeader.reqId);
  EXPECT_EQ(17, inputHeader.traceId);

  TypedThriftReply<cpp2::McGetReply> inputReply;
  apache::thrift::CompactProtocolReader reader;
  auto inputBody = folly::IOBuf::wrapBuffer(
      input.data() + inputHeader.headerSize, inputHeader.bodySize);
  reader.setInput(inputBody.get());
  inputReply.read(&reader);

  EXPECT_EQ(mc_res_remote_error, inputReply.result());
  EXPECT_TRUE(inputReply->get_message() != nullptr);
  EXPECT_EQ(message, *inputReply->get_message());
}

namespace {
void writeToBuf(folly::IOBuf& dest, const char* src, size_t len) {
  dest = folly::IOBuf(folly::IOBuf::CREATE, len);
  std::memcpy(dest.writableTail(), src, len);
  dest.append(len);
}
} // anonymous

TEST(McQueueAppender, manyFields) {
  McQueueAppenderStorage storage;
  cpp2::ManyFields tstruct;

  // Each IOBuf must have length() > 0 in order to be serialized
  const char str1[] = "abcde";
  const char str2[] = "xyzzyx";
  // Write null-terminating character too so we can use EXPECT_STREQ
  writeToBuf(tstruct.buf1, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf2, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf3, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf4, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf5, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf6, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf7, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf8, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf9, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf10, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf11, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf12, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf13, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf14, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf15, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf16, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf17, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf18, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf19, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf20, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf21, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf22, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf23, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf24, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf25, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf26, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf27, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf28, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf29, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf30, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf31, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf32, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf33, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf34, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf35, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf36, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf37, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf38, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf39, str1, std::strlen(str1) + 1);
  writeToBuf(tstruct.buf40, str2, std::strlen(str2) + 1);

  apache::thrift::CompactProtocolWriterImpl<
      McQueueAppender, McQueueAppenderStorage> writer(
          apache::thrift::SHARE_EXTERNAL_BUFFER);

  writer.setOutput(&storage);
  // This will trigger McQueueAppenderStorage::coalesce() logic
  tstruct.write(&writer);

  UmbrellaMessageInfo info;
  info.bodySize = storage.computeBodySize();
  info.typeId = 123;
  info.reqId = 456;
  info.traceId = 17;

  size_t headerSize = caretPrepareHeader(
      info, reinterpret_cast<char*>(storage.getHeaderBuf()));
  storage.reportHeaderSize(headerSize);

  folly::IOBuf input(folly::IOBuf::CREATE, 1024);
  const auto iovs = storage.getIovecs();
  for (size_t i = 0; i < iovs.second; ++i) {
    const struct iovec* iov = iovs.first + i;
    std::memcpy(input.writableTail(), iov->iov_base, iov->iov_len);
    input.append(iov->iov_len);
  }

  // Read the serialized data back in and check that it's what we wrote.
  UmbrellaMessageInfo inputHeader;
  caretParseHeader((uint8_t*)input.data(), input.length(), inputHeader);
  EXPECT_EQ(123, inputHeader.typeId);
  EXPECT_EQ(456, inputHeader.reqId);
  EXPECT_EQ(17, inputHeader.traceId);

  cpp2::ManyFields tstruct2;
  apache::thrift::CompactProtocolReader reader;
  auto inputBody = folly::IOBuf::wrapBuffer(
      input.data() + inputHeader.headerSize, inputHeader.bodySize);
  reader.setInput(inputBody.get());
  tstruct2.read(&reader);

  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf1.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf2.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf3.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf4.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf5.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf6.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf7.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf8.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf9.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf10.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf11.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf12.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf13.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf14.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf15.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf16.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf17.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf18.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf19.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf20.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf21.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf22.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf23.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf24.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf25.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf26.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf27.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf28.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf29.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf30.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf31.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf32.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf33.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf34.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf35.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf36.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf37.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf38.data()));
  EXPECT_STREQ(str1, reinterpret_cast<const char*>(tstruct2.buf39.data()));
  EXPECT_STREQ(str2, reinterpret_cast<const char*>(tstruct2.buf40.data()));
}
