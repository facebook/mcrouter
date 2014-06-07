/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <sys/mman.h>
#include <unistd.h>

#include <glog/logging.h>

namespace facebook { namespace memcache {

namespace {
size_t pagesize() {
  static const size_t pagesize = sysconf(_SC_PAGESIZE);
  return pagesize;
}

/* Returns a multiple of pagesize() enough to store size + one guard page */
size_t allocSize(size_t size) {
  return pagesize() * ((size + pagesize() - 1)/pagesize() + 1);
}
}

unsigned char* GuardPageAllocator::allocate(size_t size) {
  /* We allocate minimum number of pages required, plus a guard page.
     Since we use this for stack storage, requested allocation is aligned
     at the top of the allocated pages, while the guard page is at the bottom.

         -- increasing addresses -->
       Guard page     Normal pages
      |xxxxxxxxxx|..........|..........|
                       <- size -------->
         return value -^
   */
  void* p = nullptr;
  PCHECK(!::posix_memalign(&p, pagesize(), allocSize(size)));

  /* Try to protect first page
     (stack grows downwards from last allocated address), ignore errors */
  ::mprotect(p, pagesize(), PROT_NONE);
  /* Return pointer to top 'size' bytes in allocated storage */
  auto up = reinterpret_cast<unsigned char*>(p) + allocSize(size) - size;
  assert(up >= reinterpret_cast<unsigned char*>(p) + pagesize());
  return up;
}

void GuardPageAllocator::deallocate(unsigned char* up, size_t size) {
  /* Get allocation base */
  auto p = up + size - allocSize(size);
  /* Try to unprotect the page for memory allocator to re-use,
     ignore errors (in cases we failed to protect in the first place */
  ::mprotect(p, pagesize(), PROT_READ|PROT_WRITE);
  ::free(p);
}

}}  // facebook::memcache
