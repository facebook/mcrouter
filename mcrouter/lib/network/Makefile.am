SUBDIRS = . test

noinst_PROGRAMS = mock_mc_server
noinst_LIBRARIES = libtest_util.a

mock_mc_server_SOURCES = \
  test/MockMc.cpp \
  test/MockMc.h \
  test/MockMcOnRequest.h \
  test/MockMcServer.cpp
mock_mc_server_CPPFLAGS = -I$(top_srcdir)/..
# Link order matters.
mock_mc_server_LDADD = \
	$(top_builddir)/lib/libmcrouter.a \
	-lthriftcpp2 \
	-ltransport \
	-lthriftprotocol \
	-lReactiveSocket \
	-lyarpl \
	-lasync \
	-lconcurrency \
	-lprotocol \
	-lthrift-core \
	-lfmt \
	-lfizz \
	-lwangle \
	-lfolly

mock_mc_thrift_server_SOURCES = \
  test/MockMc.cpp \
  test/MockMc.h \
  test/MockMcOnRequest.h \
  test/MockMcThriftServer.cpp
mock_mc_thrift_server_CPPFLAGS = -I$(top_srcdir)/..
# Link order matters.
mock_mc_thrift_server_LDADD = \
	$(top_builddir)/lib/libmcrouter.a \
	-lthriftcpp2 \
	-ltransport \
	-lthriftprotocol \
	-lReactiveSocket \
	-lyarpl \
	-lasync \
	-lconcurrency \
	-lprotocol \
	-lthrift-core \
	-lfmt \
	-lfizz \
	-lwangle \
	-lfolly

mock_mc_server_dual_SOURCES = \
  test/MockMc.cpp \
  test/MockMc.h \
  test/MockMcOnRequest.h \
  test/MockMcServerDual.cpp
mock_mc_server_dual_CPPFLAGS = -I$(top_srcdir)/..
# Link order matters.
mock_mc_server_dual_LDADD = \
	$(top_builddir)/lib/libmcrouter.a \
	-lthriftcpp2 \
	-ltransport \
	-lthriftprotocol \
	-lReactiveSocket \
	-lyarpl \
	-lasync \
	-lconcurrency \
	-lprotocol \
	-lthrift-core \
	-lfmt \
	-lfizz \
	-lwangle \
	-lfolly

libtest_util_a_SOURCES = \
  test/ClientSocket.cpp \
  test/ClientSocket.h \
  test/ListenSocket.cpp \
  test/ListenSocket.h
libtest_util_a_CPPFLAGS = -I$(top_srcdir)/..
