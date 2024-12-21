BUILT_SOURCES = \
  gen-cpp2/CarbonTest_types.cpp \
  gen-cpp2/CarbonTest_types.h \
  gen-cpp2/CarbonTest_types.tcc \
  gen-cpp2/CarbonTest_data.cpp \
  gen-cpp2/CarbonTest_data.h

gen-cpp2/CarbonTest_types.cpp: gen-cpp2/CarbonTest_types.h
gen-cpp2/CarbonTest_types.h: gen-cpp2/CarbonTest_types.tcc
gen-cpp2/CarbonTest_types.tcc: gen-cpp2/CarbonTest_data.cpp
gen-cpp2/CarbonTest_data.cpp: gen-cpp2/CarbonTest_data.h
gen-cpp2/CarbonTest_data.h: CarbonTest.thrift
	@FBTHRIFT@ -gen mstch_cpp2:stack_arguments,sync_methods_return_try,deprecated_terse_writes,include_prefix=mcrouter/lib/network/test/gen/ -I $(top_srcdir)/.. -I $(INSTALL_DIR)/include/ CarbonTest.thrift
