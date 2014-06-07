#include "globals.h"

#include <unistd.h>

namespace facebook { namespace memcache { namespace globals {

uint32_t hostid() {
  static uint32_t h = gethostid();
  return h;
}

}}} // facebook::memcache
