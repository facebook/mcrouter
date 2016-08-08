/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
namespace carbon {

template <>
inline void CarbonProtocolWriter::writeRaw(const char c) {
  writeCharRaw(c);
}

template <>
inline void CarbonProtocolWriter::writeRaw(const bool b) {
  writeBoolRaw(b);
}

template <>
inline void CarbonProtocolWriter::writeRaw(const int8_t i) {
  writeInt8Raw(i);
}

template <>
inline void CarbonProtocolWriter::writeRaw(const int16_t i) {
  writeInt16Raw(i);
}

template <>
inline void CarbonProtocolWriter::writeRaw(const int32_t i) {
  writeInt32Raw(i);
}

template <>
inline void CarbonProtocolWriter::writeRaw(const int64_t i) {
  writeInt64Raw(i);
}

template <>
inline void CarbonProtocolWriter::writeRaw(const uint8_t ui) {
  writeUInt8Raw(ui);
}

template <>
inline void CarbonProtocolWriter::writeRaw(const uint16_t ui) {
  writeUInt16Raw(ui);
}

template <>
inline void CarbonProtocolWriter::writeRaw(const uint32_t ui) {
  writeUInt32Raw(ui);
}

template <>
inline void CarbonProtocolWriter::writeRaw(const uint64_t ui) {
  writeUInt64Raw(ui);
}

template <>
inline void CarbonProtocolWriter::writeRaw(const float f) {
  writeFloatRaw(f);
}

template <>
inline void CarbonProtocolWriter::writeRaw(const double d) {
  writeDoubleRaw(d);
}

} // carbon
