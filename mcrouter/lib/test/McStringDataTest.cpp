/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "folly/io/IOBuf.h"
#include "mcrouter/lib/McStringData.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;

using std::vector;

TEST(mcStringData, basic) {
  std::string s ("key");
  folly::StringPiece sp (s);
  McMsgRef msg = createMcMsgRef(sp);

  McStringData a(
      McStringData::SaveMsgKey, std::move(msg));
  auto buf_a = a.asIOBuf();
  folly::StringPiece sp_a(reinterpret_cast<const char *>(buf_a->data()),
      buf_a->length());
  EXPECT_EQ(sp, a.dataRange());

  McStringData b = a;
  auto buf_b = b.asIOBuf();
  folly::StringPiece sp_b(reinterpret_cast<const char *>(buf_b->data()),
       buf_b->length());
  EXPECT_EQ(sp, a.dataRange());
}

TEST(mcStringData, memoryRegionCheck) {
  auto value1 = McStringData(folly::IOBuf::copyBuffer("testing "));
  auto value2 = McStringData(folly::IOBuf::copyBuffer("releasedMsg"));
  vector<McStringData> string_vec;
  string_vec.push_back(value1);
  string_vec.push_back(value2);
  auto value3 = concatAll(string_vec.begin(), string_vec.end());
  auto value4 = concatAll(string_vec.begin(), string_vec.end());
  EXPECT_FALSE(value1.asIOBuf()->isChained());
  EXPECT_FALSE(value2.asIOBuf()->isChained());
  EXPECT_TRUE(value3.asIOBuf()->isChained());
  EXPECT_TRUE(value4.asIOBuf()->isChained());

  EXPECT_TRUE(value1.hasSameMemoryRegion(value1));
  EXPECT_FALSE(value1.hasSameMemoryRegion(value2));
  EXPECT_TRUE(value3.hasSameMemoryRegion(value4));
  EXPECT_FALSE(value3.hasSameMemoryRegion(value2));
}

TEST(mcStringData, copy) {
  {
    McStringData a("key");
    McStringData b = a;
    EXPECT_EQ(a.dataRange(), b.dataRange());
    EXPECT_EQ(a.dataRange().str(), "key");
  }

  { // small size that will require an internal buffer.
    McStringData a(folly::IOBuf::copyBuffer("key"));
    McStringData b = a;
    EXPECT_EQ(a.dataRange(), b.dataRange());
    EXPECT_EQ(a.dataRange().str(), "key");
  }

  { // large size that will require an external buffer.
    McStringData a(folly::IOBuf::copyBuffer(std::string(640, 'b')));
    McStringData b = a;
    // no copy when external buffer used
    EXPECT_TRUE(sameMemoryRegion(a.dataRange(), b.dataRange()));

    McStringData a_subdata = a.substr(100, 100);
    auto buf = a.asIOBuf(); // backing buffer for a_subdata
    auto buf_subdata = a_subdata.asIOBuf(); // actual data in a_subdata
    folly::StringPiece buf_sp(
      reinterpret_cast<const char*> (buf->data()), buf->length()
    );
    folly::StringPiece buf_subdata_sp(
      reinterpret_cast<const char*> (buf_subdata->data()), buf_subdata->length()
    );
    // no copy made in asIOBuf()
    EXPECT_TRUE(sameMemoryRegion(buf_sp.subpiece(100, 100), buf_subdata_sp));
  }
}

TEST(mcStringData, substr) {
  std::string s("test_subpiece_and_concat");

  {
    McStringData a(s);
    auto sp = a.dataRange();
    auto value_size = sp.size();

    int substr_size = 5;
    int num_chunks = (value_size + substr_size - 1)/ substr_size;
    size_t s_pos = 0;
    std::vector<McStringData> subdata_vec;

    // create substrings from McStringData a
    for (int i = 0; i < num_chunks; i++) {
      auto len = ((s_pos + substr_size) >= value_size) ?
        std::string::npos : substr_size;
      subdata_vec.push_back(a.substr(s_pos, len));

      auto sp_piece = sp.subpiece(s_pos, len);
      EXPECT_EQ(subdata_vec[i].dataRange(), sp_piece);
      EXPECT_EQ(subdata_vec[i].dataRange().toString(), sp_piece.toString());

      auto buf = subdata_vec[i].asIOBuf();
      auto sp_buf = folly::StringPiece(
        reinterpret_cast<const char*> (buf->data()), buf->length());
      EXPECT_EQ(sp_buf, sp_piece);
      EXPECT_EQ(sp_buf.toString(), sp_piece.toString());

      s_pos += len;
    }

    // McStringData a has the same datarange
    EXPECT_EQ(a.dataRange().toString(), s);

    McStringData concatstr = concatAll(subdata_vec.begin(), subdata_vec.end());
    EXPECT_EQ(concatstr.dataRange(), sp);
    EXPECT_EQ(concatstr.dataRange().toString(), sp.toString());
  }

  { // empty McStringData
    McStringData s_empty;
    auto sub_str = s_empty.substr(0);
    EXPECT_EQ(s_empty.dataRange().str(), "");
  }

  { // substr length greater than orignal string length
    McStringData str(s);
    auto sub_str = str.substr(0, 100);
    EXPECT_EQ(str.dataRange().str(), sub_str.dataRange().str());
  }

  { // substr for a McStringData with non-zero begin in relative range
    McStringData str(s);
    auto sub_str1 = str.substr(3);
    auto sub_str2 = str.substr(2);
    auto sub_str3 = sub_str2.substr(1);
    EXPECT_EQ(sub_str1.dataRange().str(), sub_str3.dataRange().str());
  }
}
