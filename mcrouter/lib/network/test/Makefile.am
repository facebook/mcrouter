check_PROGRAMS = mcrouter_network_test

BUILT_SOURCES = \
  ../gen-cpp2/mock_protocol_types.cpp \
  ../gen-cpp2/mock_protocol_types.h

# The `sed` hack below is necessary until the Thrift compiler is modified
# to not generate service-related includes when linking only against
# libthriftprotocol.
../gen-cpp2/mock_protocol_types.cpp: ../gen-cpp2/mock_protocol_types.h
../gen-cpp2/mock_protocol_types.h: mock_protocol.thrift
	PYTHONPATH=$(THRIFT2_COMP_DIR)/..:$$PYTHONPATH $(THRIFT) --gen cpp2 -o ../ $<
	find ../gen-cpp2/ -name 'mock_protocol*' -type f | xargs sed -i 's/^.*GeneratedCodeHelper\.h.*$$//g'
	find ../gen-cpp2/ -name 'mock_protocol*' -type f | xargs sed -i 's/^.*Cpp2ConnContext\.h.*$$//g'


mcrouter_network_test_SOURCES = \
  AccessPointTest.cpp \
  AsyncMcClientCaretTest.cpp \
  AsyncMcClientTestSync.cpp \
  ../gen-cpp2/mock_protocol_types.cpp \
  ../gen-cpp2/mock_protocol_types.h \
  McAsciiParserTest.cpp \
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
