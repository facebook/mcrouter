check_PROGRAMS = mcrouter_network_test

BUILT_SOURCES = \
  caret_test_types.cpp \
  caret_test_types.h \
  caret_test_types_custom_protocol.h

caret_test_types.cpp: caret_test_types.h
caret_test_types_custom_protocol.h: caret_test_types.h
caret_test_types.h: caret_test.thrift
	PYTHONPATH=$(THRIFT2_COMP_DIR)/..:$$PYTHONPATH $(THRIFT) --gen cpp2 -o ../ $<
	find ../gen-cpp2/ -name 'caret_test*' -type f | xargs sed -i 's/^.*GeneratedCodeHelper\.h.*$$//g'
	find ../gen-cpp2/ -name 'caret_test*' -type f | xargs sed -i 's/^.*Cpp2ConnContext\.h.*$$//g'

mcrouter_network_test_SOURCES = \
  AccessPointTest.cpp \
  AsyncMcClientTestSync.cpp \
  ../gen-cpp2/caret_test_types.cpp \
  ../gen-cpp2/caret_test_types.h \
  ../gen-cpp2/caret_test_types_custom_protocol.h \
  ../gen-cpp2/mc_caret_protocol_types.cpp \
  ../gen-cpp2/mc_caret_protocol_types.h \
  ../gen-cpp2/mc_caret_protocol_types_custom_protocol.h \
  McAsciiParserTest.cpp \
  McQueueAppenderTest.cpp \
  McServerAsciiParserTest.cpp \
  MockMc.cpp \
  MockMcServer.cpp \
  SessionTest.cpp \
  SessionTestHarness.cpp \
  SessionTestHarness.h \
  TestClientServerUtil.cpp \
  TestClientServerUtil.h \
  TestMcAsciiParserUtil.cpp \
  TestMcAsciiParserUtil.h \
  ThriftMockMcTest.cpp \
  ThriftMsgDispatcherTest.cpp

mcrouter_network_test_CPPFLAGS = -I$(top_srcdir)/.. -I$(top_srcdir)/lib/gtest/include
mcrouter_network_test_LDADD = \
  $(top_builddir)/lib/libmcrouter.a \
  $(top_builddir)/lib/network/libtest_util.a \
  $(top_builddir)/lib/libgtestmain.la
