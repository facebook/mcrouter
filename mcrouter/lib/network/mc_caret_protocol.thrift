namespace cpp2 facebook.memcache.cpp2

typedef binary (cpp2.type = "folly::IOBuf") IOBuf

struct McGetRequest {
  1: IOBuf key;
}

struct McGetReply {
  1: i16 result;
  2: optional IOBuf value;
  3: optional i64 flags;
  4: optional string message;
}

struct McSetRequest {
  1: IOBuf key;
  2: i32 exptime;
  3: i64 flags;
  4: IOBuf value;
}

struct McSetReply {
  1: i16 result;
  2: optional string message;
}

struct McDeleteRequest {
  1: IOBuf key;
  2: optional i32 exptime;
}

struct McDeleteReply {
  1: i16 result;
  2: optional string message;
}

struct McLeaseGetRequest {
  1: IOBuf key;
}

struct McLeaseGetReply {
  1: i16 result;
  2: optional i64 leaseToken;
  3: optional IOBuf value;
  4: optional i64 flags;
  5: optional string message;
}

struct McLeaseSetRequest {
  1: IOBuf key;
  2: i32 exptime;
  3: i64 flags;
  4: IOBuf value;
  5: i64 leaseToken;
}

struct McLeaseSetReply {
  1: i16 result;
  2: optional string message;
}

struct McAddRequest {
  1: IOBuf key;
  2: i32 exptime;
  3: i64 flags;
  4: IOBuf value;
}

struct McAddReply {
  1: i16 result;
  2: optional string message;
}

struct McReplaceRequest {
  1: IOBuf key;
  2: i32 exptime;
  3: i64 flags;
  4: IOBuf value;
}

struct McReplaceReply {
  1: i16 result;
  2: optional string message;
}

struct McGetsRequest {
  1: IOBuf key;
}

struct McGetsReply {
  1: i16 result;
  2: optional i64 casToken;
  3: optional IOBuf value;
  4: optional i64 flags;
  5: optional string message;
}

struct McCasRequest {
  1: IOBuf key;
  2: i32 exptime;
  3: i64 flags;
  4: IOBuf value;
  5: i64 casToken;
}

struct McCasReply {
  1: i16 result;
  2: optional string message;
}

struct McIncrRequest {
  1: IOBuf key;
  2: i64 delta;
}

struct McIncrReply {
  1: i16 result;
  2: optional i64 delta;
  3: optional string message;
}

struct McDecrRequest {
  1: IOBuf key;
  2: i64 delta;
}

struct McDecrReply {
  1: i16 result;
  2: optional i64 delta;
  3: optional string message;
}

struct McMetagetRequest {
  1: IOBuf key;
}

struct McMetagetReply {
  1: i16 result;
  2: optional i32 age;
  3: optional i32 exptime;
  4: optional bool isTransient;
  5: optional i16 ipv;
  6: optional string ipAddress;
  7: optional string message;
}

struct McVersionRequest {
  // TODO(jmswen) Can we get rid of this hack?
  1: IOBuf key;
}

struct McVersionReply {
  1: i16 result;
  2: optional string version;
  3: optional string message;
}

struct McAppendRequest {
  1: IOBuf key;
  2: i32 exptime;
  3: i64 flags;
  4: IOBuf value;
}

struct McAppendReply {
  1: i16 result;
  2: optional string message;
}

struct McPrependRequest {
  1: IOBuf key;
  2: i32 exptime;
  3: i64 flags;
  4: IOBuf value;
}

struct McPrependReply {
  1: i16 result;
  2: optional string message;
}

struct McTouchRequest {
  1: IOBuf key;
  2: optional i32 exptime;
}

struct McTouchReply {
  1: i16 result;
  2: optional string message;
}
