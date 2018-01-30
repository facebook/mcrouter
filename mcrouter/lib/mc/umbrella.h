/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <stdint.h>
#include <stdlib.h>

#include "mcrouter/lib/fbi/decls.h"
#include "mcrouter/lib/mc/msg.h"

__BEGIN_DECLS

#define UMBRELLA_VERSION_BASIC 0

// a '}' looks like an umbrella on it's side, obviously
#define ENTRY_LIST_MAGIC_BYTE '}'

// An entry list is made up of a series of entries

// Each entry has a type
typedef enum entry_type_e {
  UNDEF = 0,
  I32, // int32_t
  U32, // uint32_t
  I64, // int64_t
  U64, // uint64_t
  CSTRING, // char * (null terminated c string
  BSTRING, // char * (binary blob)
} entry_type_t;

typedef struct um_elist_entry_s {
  uint16_t type; // What's the type of this entry?
  uint16_t tag; // Application-specific tag for this entry
  union {
    struct { // If it's a string:
      uint32_t offset; //   How far into the body does the string start?
      uint32_t len; //   How long is the string?
    } str;
    uint64_t val; // If it's an int, just throw it into a uint64_t
  } data;
} __attribute__((__packed__)) um_elist_entry_t;

typedef struct entry_list_hdr_s {
  uint8_t magic_byte;
  uint8_t version;
} entry_list_hdr_t;

// The wire-protocol header of the entry list
typedef struct entry_list_msg_s {
  entry_list_hdr_t msg_header; // Mostly for the magic byte
  uint16_t nentries; // How many entries are there
  uint32_t total_size; // MUST be first! (for length-prefixed-ness)
  um_elist_entry_t entries[0]; // The entries follow
  // And then the body comes.  len(body) = total_size - len(entries)
} __attribute__((__packed__)) entry_list_msg_t;

typedef enum msg_field_e {
  msg_undefined = 0,

  msg_op = 0x1,
  msg_result = 0x2,
  msg_reqid = 0x4,
  msg_err_code = 0x8,

  msg_flags = 0x10,
  msg_exptime = 0x20,
  msg_number = 0x40,

  msg_delta = 0x100,
  msg_lease_id = 0x200,
  msg_cas = 0x400,
#ifndef LIBMC_FBTRACE_DISABLE
  msg_fbtrace = 0x800,
#endif

  msg_stats = 0x1000,
  msg_key = 0x2000,
  msg_value = 0x4000,
  // These values must fit in a short, so 0x8000 is the max
  // with this scheme.
} msg_field_t;

#define UM_NOPS 29

extern uint32_t const umbrella_op_from_mc[UM_NOPS];
extern uint32_t const umbrella_op_to_mc[UM_NOPS];

extern uint32_t const umbrella_res_from_mc[mc_nres];
extern uint32_t const umbrella_res_to_mc[mc_nres];

__END_DECLS
