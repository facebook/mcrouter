/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include "mcrouter/lib/mc/umbrella.h"
#include "mcrouter/lib/mc/umbrella_protocol.h"

int emitIov(void* ctxt, const void* buf, size_t len) {
  assert(ctxt != nullptr);
  ((std::string *)ctxt)->append((const char *)buf, len);
  return 0;
}

void verifySerializedValue(const std::string& serializedVal,
                           uint64_t expectedReqId,
                           mc_msg_t* expectedMsg,
                           const char* value) {
  // check if the value was serialized correctly
  um_parser_t parser;
  EXPECT_EQ(um_parser_init(&parser), 0);

  uint64_t parsedReqId = -1;
  mc_msg_t* parsedMsg = nullptr;

  EXPECT_EQ(um_consume_one_message(&parser,
                                   (uint8_t *)serializedVal.c_str(),
                                   serializedVal.length(),
                                   &parsedReqId,
                                   &parsedMsg),
            serializedVal.length());

  EXPECT_NE(parsedMsg, nullptr);
  EXPECT_EQ(parsedReqId, expectedReqId);
  EXPECT_EQ(parsedMsg->op, expectedMsg->op);
  EXPECT_EQ(parsedMsg->result, expectedMsg->result);
  EXPECT_EQ(parsedMsg->flags, expectedMsg->flags);
  EXPECT_EQ(parsedMsg->value.len, strlen(value));
  EXPECT_EQ(strncmp(parsedMsg->value.str, value, strlen(value)), 0);

  mc_msg_decref(parsedMsg);
}

TEST(umbrella, iovecSerialize) {
  size_t nIovs = 3;
  char value[] = "value";
  iovec iovs[] = {{&value[0], 1}, {&value[1], 2}, {&value[3], 2}};

  um_backing_msg_t bmsg;
  um_backing_msg_init(&bmsg);

  uint64_t reqId = 1;
  mc_msg_t msg;
  mc_msg_init_not_refcounted(&msg);
  msg.op = mc_op_get;
  msg.result = mc_res_found;
  msg.flags = 0x4;

  // Verify emitIov
  std::string serializedVal = "";
  EXPECT_EQ(
      um_emit_iovs_extended(
          &bmsg, reqId, &msg, iovs, nIovs, emitIov, (void *)&serializedVal),
      0);

  verifySerializedValue(serializedVal, reqId, &msg, value);


  // Verify writeIov
  um_backing_msg_cleanup(&bmsg);
  um_backing_msg_init(&bmsg);
  serializedVal = "";
  reqId++;

  iovec output[20];
  ssize_t nOutIovs = um_write_iovs_extended(&bmsg, reqId, &msg,
                                            iovs, nIovs, output, 20);
  EXPECT_GE(nOutIovs, 1);
  for (int i = 0; i < nOutIovs; i++) {
    serializedVal.append((const char *)output[i].iov_base, output[i].iov_len);
  }

  verifySerializedValue(serializedVal, reqId, &msg, value);
}
