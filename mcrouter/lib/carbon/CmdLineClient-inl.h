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

#include <functional>

namespace carbon {
namespace tools {

template <class Client>
void CmdLineClient::sendRequests(int argc, const char** argv) {
  auto settings = parseSettings(argc, argv);
  Client jsonClient(settings.clientOptions, [this](const std::string& msg) {
    targetErr_ << msg << std::endl;
  });
  sendRequests(jsonClient, settings.requestName, settings.data);
}

} // tools
} // carbon
