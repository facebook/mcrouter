namespace cpp2 facebook.memcache.cpp2

typedef binary (cpp2.type = "folly::IOBuf") IOBuf

struct McGetRequest {
  1: IOBuf key;
  2: optional i64 flags;
  3: optional i32 exptime;
}

struct McGetReply {
  1: i16 result;
  2: optional IOBuf value;
  3: optional i64 flags;
  4: optional string message;
  // appSpecificErrorCode is a short-term hack that will be removed from all
  // Mc*Reply structures in the future.
  5: optional i16 appSpecificErrorCode;
}

struct McSetRequest {
  1: IOBuf key;
  2: i32 exptime;
  3: i64 flags;
  4: IOBuf value;
}

struct McSetReply {
  1: i16 result;
  2: optional i64 flags;
  3: optional IOBuf value;
  4: optional string message;
  5: optional i16 appSpecificErrorCode;
}

struct McDeleteRequest {
  1: IOBuf key;
  2: optional i64 flags;
  3: optional i32 exptime;
  4: optional IOBuf value;
}

struct McDeleteReply {
  1: i16 result;
  2: optional i64 flags;
  3: optional IOBuf value;
  4: optional string message;
  5: optional i16 appSpecificErrorCode;
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
  6: optional i16 appSpecificErrorCode;
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
  3: optional i16 appSpecificErrorCode;
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
  3: optional i16 appSpecificErrorCode;
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
  3: optional i16 appSpecificErrorCode;
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
  6: optional i16 appSpecificErrorCode;
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
  3: optional i16 appSpecificErrorCode;
}

struct McIncrRequest {
  1: IOBuf key;
  2: i64 delta;
}

struct McIncrReply {
  1: i16 result;
  2: optional i64 delta;
  3: optional string message;
  4: optional i16 appSpecificErrorCode;
}

struct McDecrRequest {
  1: IOBuf key;
  2: i64 delta;
}

struct McDecrReply {
  1: i16 result;
  2: optional i64 delta;
  3: optional string message;
  4: optional i16 appSpecificErrorCode;
}

struct McMetagetRequest {
  1: IOBuf key;
}

struct McMetagetReply {
  1: i16 result;
  2: optional i32 age;
  3: optional i32 exptime;
  4: optional i16 ipv;
  5: optional string ipAddress;
  6: optional string message;
  7: optional i16 appSpecificErrorCode;
}

struct McVersionRequest {
  // TODO(jmswen) Can we get rid of this hack?
  1: IOBuf key;
}

struct McVersionReply {
  1: i16 result;
  2: optional IOBuf value;
  3: optional string message;
  4: optional i16 appSpecificErrorCode;
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
  3: optional i16 appSpecificErrorCode;
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
  3: optional i16 appSpecificErrorCode;
}

struct McTouchRequest {
  1: IOBuf key;
  2: i32 exptime;
}

struct McTouchReply {
  1: i16 result;
  2: optional string message;
  3: optional i16 appSpecificErrorCode;
}

struct McStatsRequest {
  1: IOBuf key;
}

struct McStatsReply {
  1: i16 result;
  2: optional string message;
  3: optional list<string> stats;
  4: optional i16 appSpecificErrorCode;
}

struct McShutdownRequest {
  1: IOBuf key; // unused
}

struct McShutdownReply {
  1: i16 result;
  2: optional string message;
  3: optional i16 appSpecificErrorCode;
}

struct McQuitRequest {
  1: IOBuf key; // unused
}

struct McQuitReply {
  1: i16 result;
  2: optional string message;
  3: optional i16 appSpecificErrorCode;
}

struct McExecRequest {
  1: IOBuf key;
}

struct McExecReply {
  1: i16 result;
  2: optional string response;
  3: optional string message;
  4: optional i16 appSpecificErrorCode;
}

struct McFlushReRequest {
  1: IOBuf key;
}

struct McFlushReReply {
  1: i16 result;
  2: optional string message;
  3: optional i16 appSpecificErrorCode;
}

struct McFlushAllRequest {
  1: IOBuf key; // unused
  2: optional i32 delay;
}

struct McFlushAllReply {
  1: i16 result;
  2: optional string message;
  3: optional i16 appSpecificErrorCode;
}
