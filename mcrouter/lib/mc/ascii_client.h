/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FB_MEMCACHE_MC_ASCII_CLIENT_H
#define FB_MEMCACHE_MC_ASCII_CLIENT_H

#include "mcrouter/lib/mc/parser.h"

int _on_ascii_rx(mc_parser_t *parser, char* buf, size_t nbuf);

#endif // libmc_ASCII_H
