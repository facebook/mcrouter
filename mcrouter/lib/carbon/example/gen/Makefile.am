noinst_LIBRARIES = libcarbon_hello_goodbye.a

thrift_sources = \
  gen-cpp2/HelloGoodbye_types.cpp \
  gen-cpp2/HelloGoodbye_types.h \
  gen-cpp2/HelloGoodbye_types.tcc \
  gen-cpp2/HelloGoodbye_data.cpp \
  gen-cpp2/HelloGoodbye_data.h

BUILT_SOURCES = $(thrift_sources)

gen-cpp2/HelloGoodbye_types.cpp: gen-cpp2/HelloGoodbye_types.h $(top_srcdir)/lib/network/gen/gen-cpp2/Common_types.h
gen-cpp2/HelloGoodbye_types.h: gen-cpp2/HelloGoodbye_types.tcc
gen-cpp2/HelloGoodbye_types.tcc: gen-cpp2/HelloGoodbye_data.cpp
gen-cpp2/HelloGoodbye_data.cpp: gen-cpp2/HelloGoodbye_data.h
gen-cpp2/HelloGoodbye_data.h: HelloGoodbye.thrift
	@FBTHRIFT@ -gen mstch_cpp2:stack_arguments,sync_methods_return_try,deprecated_terse_writes,include_prefix=mcrouter/lib/carbon/example/gen/ -I $(top_srcdir)/.. -I $(INSTALL_DIR)/include/ HelloGoodbye.thrift

libcarbon_hello_goodbye_a_SOURCES = \
  $(thrift_sources) \
  HelloGoodbyeMessages.h \
  HelloGoodbyeMessages-inl.h \
  HelloGoodbyeMessagesThrift.cpp

libcarbon_hello_goodbye_a_CPPFLAGS = -I$(top_srcdir)/..
