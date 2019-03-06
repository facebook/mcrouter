BUILT_SOURCES = \
  gen-cpp2/Common_types.cpp \
  gen-cpp2/Common_types.h \
  gen-cpp2/Common_types.tcc \
  gen-cpp2/Common_data.cpp \
  gen-cpp2/Common_data.h \
  gen-cpp2/Memcache_types.cpp \
  gen-cpp2/Memcache_types.h \
  gen-cpp2/Memcache_types.tcc \
  gen-cpp2/Memcache_data.cpp \
  gen-cpp2/Memcache_data.h

gen-cpp2/Common_types.cpp: gen-cpp2/Common_types.h
gen-cpp2/Common_types.h: gen-cpp2/Common_types.tcc
gen-cpp2/Common_types.tcc: gen-cpp2/Common_data.cpp
gen-cpp2/Common_data.cpp: gen-cpp2/Common_data.h
gen-cpp2/Common_data.h: Common.thrift
	@FBTHRIFT@ -gen mstch_cpp2:terse_writes,include_prefix=mcrouter/lib/network/gen -I $(top_srcdir)/.. Common.thrift

gen-cpp2/Memcache_types.cpp: gen-cpp2/Memcache_types.h
gen-cpp2/Memcache_types.h: gen-cpp2/Memcache_types.tcc
gen-cpp2/Memcache_types.tcc: gen-cpp2/Memcache_data.cpp
gen-cpp2/Memcache_data.cpp: gen-cpp2/Memcache_data.h
gen-cpp2/Memcache_data.h: Memcache.thrift
	@FBTHRIFT@ -gen mstch_cpp2:terse_writes,include_prefix=mcrouter/lib/network/gen -I $(top_srcdir)/.. Memcache.thrift
