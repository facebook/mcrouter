/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>
#include <vector>

#include "mcrouter/lib/carbon/CarbonProtocolReader.h"
#include "mcrouter/lib/carbon/CarbonProtocolWriter.h"
#include "mcrouter/lib/carbon/Fields.h"

namespace carbon {
namespace test {

typedef struct {
  std::string name;
  std::vector<int> points;
} UserType;
} // test

template <>
struct SerializationTraits<carbon::test::UserType> {
  static constexpr carbon::FieldType kWireType = carbon::FieldType::Struct;

  static carbon::test::UserType read(carbon::CarbonProtocolReader& reader) {
    carbon::test::UserType readType;
    reader.readStructBegin();
    while (true) {
      const auto pr = reader.readFieldHeader();
      const auto fieldType = pr.first;
      const auto fieldId = pr.second;

      if (fieldType == carbon::FieldType::Stop) {
        break;
      }

      switch (fieldId) {
        case 1: {
          reader.readRawInto(readType.name);
          break;
        }
        case 2: {
          reader.readRawInto(readType.points);
          break;
        }
        default: {
          reader.skip(fieldType);
          break;
        }
      }
    }
    reader.readStructEnd();
    return readType;
  }

  static void write(
      const carbon::test::UserType& writeType,
      carbon::CarbonProtocolWriter& writer) {
    writer.writeStructBegin();
    writer.writeField(1 /* field id */, writeType.name);
    writer.writeField(2 /* field id */, writeType.points);
    writer.writeStructEnd();
    writer.writeStop();
  }

  static bool isEmpty(const carbon::test::UserType& writeType) {
    return writeType.name.empty() && writeType.points.empty();
  }
};
}
