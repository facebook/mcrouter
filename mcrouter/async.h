/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <string>

#include <folly/Range.h>

class mc_msg_s;
using mc_msg_t = mc_msg_s;

namespace facebook { namespace memcache { namespace mcrouter {

class awriter_t;
class writelog_entry_t;
class proxy_request_t;
class ProxyClientCommon;

enum asynclog_event_type_t {
  asynclog_event_unknown = 0,
  asynclog_event_command = 'C',
};

// Appends an entry for file writes
void asynclog_command(proxy_request_t *preq,
                      std::shared_ptr<const ProxyClientCommon> pclient,
                      const mc_msg_t* req,
                      folly::StringPiece poolName);

// Write contents to file 'path' asynchronously.
// File or parent directories will be created if necessary.
// File write is atomic (write to tempfile and rename)
// Returns 0 on success, -1 on error.
int async_write_file(awriter_t* awriter,
                     const std::string& path,
                     const std::string& contents);

void writelog_entry_free(writelog_entry_t *e);

}}} // facebook::memcache::mcrouter
