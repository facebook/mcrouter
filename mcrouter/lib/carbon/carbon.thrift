namespace cpp2 carbon.thrift

cpp_include "<mcrouter/lib/carbon/Keys.h>"

typedef binary (cpp.type = "carbon::Keys<folly::IOBuf>",
                cpp.indirection = ".rawUnsafe()") IOBufKey

typedef binary (cpp.type = "carbon::Keys<std::string>",
                cpp.indirection = ".rawUnsafe()") StringKey
