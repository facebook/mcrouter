/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

namespace carbon {

class CarbonProtocolReader;
class CarbonProtocolWriter;

/*
 * A user type may be used in a Carbon structure if the user provides an
 * appropriate specialization of carbon::SerializationTraits.  The following
 * methods should be provided in such a specialization:
 *
 * template <class T>
 * struct SerializationTraits;
 *   static T read(CarbonProtocolReader&);
 *   static void write(const T&, CarbonProtocolReader&);
 *   static bool isEmpty(const T&);
 * };
 */
template <class T>
struct SerializationTraits;

} // carbon
