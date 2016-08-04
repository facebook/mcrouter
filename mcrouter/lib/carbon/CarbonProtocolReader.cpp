/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CarbonProtocolReader.h"

namespace carbon {

void CarbonProtocolReader::skip(const FieldType fieldType) {
  switch (fieldType) {
    case FieldType::True:
    case FieldType::False: {
      break;
    }
    case FieldType::Int8: {
      readInt8Field();
      break;
    }
    case FieldType::Int16: {
      readInt16Field();
      break;
    }
    case FieldType::Int32: {
      readInt32Field();
      break;
    }
    case FieldType::Int64: {
      readInt64Field();
      break;
    }
    case FieldType::Double: {
      readDoubleField();
      break;
    }
    case FieldType::Float: {
      readFloatField();
      break;
    }
    case FieldType::Binary: {
      readBinaryField<std::string>();
      break;
    }
    case FieldType::List: {
      const auto pr = readListFieldSizeAndInnerType();
      const auto fieldType = pr.first;
      const auto len = pr.second;
      for (size_t i = 0; i < len; ++i) {
        skip(fieldType);
      }
      break;
    }
    case FieldType::Struct: {
      readStructBegin();
      while (true) {
        const auto fieldType = readFieldHeader().first;
        if (fieldType == FieldType::Stop) {
          break;
        }
        skip(fieldType);
      }
      readStructEnd();
      break;
    }
    default: { break; }
  }
}

} // carbon
