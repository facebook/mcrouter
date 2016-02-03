namespace cpp2 facebook.memcache.cpp2

typedef binary (cpp2.type = "folly::IOBuf") IOBuf

struct MockMcGetRequest {
  1: string key;
}

struct MockMcSetRequest {
  1: string key;
  2: IOBuf value;
  3: optional i32 exptime;
  4: optional i64 flags;
}

struct MockMcDeleteRequest {
  1: string key;
}
