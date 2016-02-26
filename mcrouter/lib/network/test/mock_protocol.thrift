namespace cpp2 facebook.memcache.cpp2

struct FooRequest {
  1: i64 id;
  2: optional string data;
  3: string key;
}

struct BarRequest {
  1: optional i32 something;
  2: optional string other;
  3: string key;
}
