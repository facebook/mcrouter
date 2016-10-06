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

void CarbonProtocolReader::skip(const FieldType ft) {
  switch (ft) {
    case FieldType::True:
    case FieldType::False: {
      break;
    }
    case FieldType::Int8: {
      readRaw<int8_t>();
      break;
    }
    case FieldType::Int16: {
      readRaw<int16_t>();
      break;
    }
    case FieldType::Int32: {
      readRaw<int32_t>();
      break;
    }
    case FieldType::Int64: {
      readRaw<int64_t>();
      break;
    }
    case FieldType::Double: {
      readRaw<double>();
      break;
    }
    case FieldType::Float: {
      readRaw<float>();
      break;
    }
    case FieldType::Binary: {
      readRaw<std::string>();
      break;
    }
    case FieldType::List: {
      const auto pr = readVectorFieldSizeAndInnerType();
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
