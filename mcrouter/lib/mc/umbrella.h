/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FB_MEMCACHE_MC_UMBRELLA_H
#define FB_MEMCACHE_MC_UMBRELLA_H

#include <arpa/inet.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

#include "mcrouter/lib/fbi/debug.h"
#include "mcrouter/lib/fbi/decls.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/umbrella_protocol.h"

__BEGIN_DECLS

#define UMBRELLA_VERSION_BASIC 0

#define flip64(f, x) ((int64_t)(f)(((int64_t)x) >> 32) | ((int64_t)(f)((int32_t)(x)) << 32))

#define ntoh64(x) (flip64(ntohl, ((int64_t)x)))
#define ntoh32(x) ntohl(x)
#define ntoh16(x) ntohs(x)
#define ntoh8(x) (x)

#define hton64(x) (flip64(htonl, ((int64_t)x)))
#define hton32(x) htonl(x)
#define hton16(x) htons(x)
#define hton8(x) (x)

// An entry list is made up of a series of entries

// Each entry has a type
typedef enum entry_type_e {
  UNDEF = 0,
  I32,                      // int32_t
  U32,                      // uint32_t
  I64,                      // int64_t
  U64,                      // uint64_t
  CSTRING,                  // char * (null terminated c string
  BSTRING,                  // char * (binary blob)
} entry_type_t;

// To avoid an allocation and data copy, we allow entries to point to strings
// outside of the body.  This is inlined when written to a buffer
typedef struct extern_string_s {
  int entry_idx;      // Which entry are we pointing to?
  const void *ptr;    // Where does the string actually live?
  uint32_t len;       // How long is it?
} extern_string_t;

// Similar to extern strings, extern iovs allow entries to point to externally
// allocated iovecs. This is only used while creating a serialized reply.
// Over the wire these are sent as BSTRING obtained by concatenating all iovecs
// and a terminating null-byte
typedef struct extern_iov_s {
  int entry_idx;            // Which entry are we pointing to?
  struct iovec* iovs;       // Pointer to the array of iovs
  size_t niovs;             // Number of iovecs in iovs
} extern_iov_t;

typedef struct um_elist_entry_s {
  uint16_t type;        // What's the type of this entry?
  uint16_t tag;         // Application-specific tag for this entry
  union {
    struct {            // If it's a string:
      uint32_t offset;  //   How far into the body does the string start?
      uint32_t len;     //   How long is the string?
    } str;
    uint64_t val;       // If it's an int, just throw it into a uint64_t
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
} __attribute__((__packed__))  entry_list_msg_t;

// Prefixed message length preparer
//  - Mini state machine that we can feed raw bufs into
//  - is finished when it has received the entire buffer
typedef struct entry_list_msg_preparer_s {
  uint32_t nbuf;
  uint32_t expected;
  char *msgbuf;
  int finished;
  int magic_byte;
  char msg_header_buf[sizeof(entry_list_msg_t)];
} entry_list_msg_preparer_t;

// tunable parameters
#define MAX_EXTERN_STRINGS 16
#define BASE_ENTRIES_SIZE 16
#define BASE_BODY_SIZE 1024

// entry list use struct
//  We can initialize this bad boy directly from the enty_list_msg_t
//    pretty quickly
//  We can also use it to build messages to send.  Grows itself as necessary.
typedef struct entry_list_s {
  // Only free the backing_message if it's been allocated by us or if we
  // adopted it
  int backing_message_allocated;
  char *backing_message; // buffer from which this is built.  entries & body

  uint32_t entries_allocated; // only free the entries array if we allocated it
  uint32_t entries_size;      // total entries array size
  uint32_t nentries;          // entries array slots used
  um_elist_entry_t *entries;           // entries array

  // As mentioned before, external strings allow us to avoid data copies
  uint32_t nestrings; // number of external strings
  extern_string_t estrings[MAX_EXTERN_STRINGS]; // external strings
  uint32_t total_estrings_len; // total length of external strings

  extern_iov_t eiov;       // we allow only one external iov array for now
  uint32_t total_eiov_len; // sum of the lengths of all iovs

  entry_list_msg_t msg;    // So we have something for iovecs to point to

  uint32_t body_allocated; // only free the body if we allocated it
  uint32_t body_size;      // total body size
  uint32_t nbody;          // body bytes used
  void *body;              // pointer to body
} entry_list_t;

int entry_list_init(entry_list_t *elist);
void entry_list_cleanup(entry_list_t *elist);
ssize_t entry_list_write_to_buf(entry_list_t *elist, char *buf, ssize_t len);
ssize_t entry_list_read_from_buf(entry_list_t *elist,
                                 char *buf, size_t len,
                                 char *body, size_t nbody,
                                 int free_buf_when_done);

int entry_list_emit_iovs(entry_list_t* elist, emit_iov_cb* emit_iov,
                         void* context);

int entry_list_to_iovecs(entry_list_t *elist, struct iovec *vecs, int max);

#define APPEND_INT_FUNC(etype, ctype)                                   \
  int entry_list_append_ ## etype (entry_list_t *elist,                 \
                                   int32_t tag,                         \
                                   ctype x);

APPEND_INT_FUNC(I32, int32_t)
APPEND_INT_FUNC(U32, uint32_t)
APPEND_INT_FUNC(I64, int64_t)
APPEND_INT_FUNC(U64, uint64_t)

#undef APPEND_INT_FUNC

int entry_list_append_BSTRING(entry_list_t *elist,
                              int32_t tag,
                              const char *str,
                              int len);
int entry_list_append_CSTRING(entry_list_t *elist,
                              int32_t tag,
                              const char *str);
int entry_list_lazy_append_BSTRING(entry_list_t *elist,
                                   int32_t tag,
                                   const char *str,
                                   int len);
int entry_list_lazy_append_CSTRING(entry_list_t *elist,
                                   int32_t tag,
                                   const char *str);
int entry_list_lazy_append_IOVEC(entry_list_t *elist,
                                 int32_t tag,
                                 struct iovec* iovs,
                                 size_t niovs);

void print_entry_list(entry_list_t *elist);

void entry_list_preparer_init(entry_list_msg_preparer_t *prep);
void entry_list_preparer_reset_after_failure(
  entry_list_msg_preparer_t *prep);
ssize_t entry_list_preparer_read(entry_list_msg_preparer_t *prep,
                                 const char *buf,
                                 ssize_t len);
ssize_t entry_list_consume_preparer(entry_list_t *elist,
                                    entry_list_msg_preparer_t *prep);

int entry_list_preparer_finished(entry_list_msg_preparer_t *prep);


#define BMSG_ENTRIES_ARRAY_SIZE 8

struct um_backing_msg_s {
  entry_list_t elist;
  um_elist_entry_t entries_array[BMSG_ENTRIES_ARRAY_SIZE];
  mc_msg_t* msg;
  int inuse;
};

struct um_parser_s {
  entry_list_msg_preparer_t prep;
};

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

#define UM_NOPS 28

extern uint32_t const umbrella_op_from_mc[UM_NOPS];
extern uint32_t const umbrella_op_to_mc[UM_NOPS];

extern uint32_t const umbrella_res_from_mc[mc_nres];
extern uint32_t const umbrella_res_to_mc[mc_nres];

__END_DECLS

#endif
