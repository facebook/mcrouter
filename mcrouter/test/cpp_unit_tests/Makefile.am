# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

check_PROGRAMS = mcrouter_test

mcrouter_test_SOURCES = \
	main.cpp \
  awriter_test.cpp \
  config_api_test.cpp \
  exponential_smooth_data_test.cpp \
  file_observer_test.cpp \
  flavor_test.cpp \
  LeaseTokenMapTest.cpp \
  mc_route_handle_provider_test.cpp \
  observable_test.cpp \
  options_test.cpp \
  pool_factory_test.cpp \
  ProxyRequestContextTest.cpp \
  route_test.cpp \
  runtime_vars_data_test.cpp

# Note: McrouterClientUsage.cpp is excluded
# since it depends on HelloGoodbyeService and fb303.

mcrouter_test_CPPFLAGS = \
	-I$(top_srcdir)/.. \
	-isystem $(top_srcdir)/lib/gtest/include

mcrouter_test_LDADD = \
  $(top_builddir)/libmcroutercore.a \
  $(top_builddir)/lib/libmcrouter.a \
  $(top_builddir)/lib/libtestmain.la \
  $(top_builddir)/lib/network/test/libtest_util.a \
  $(top_builddir)/lib/carbon/example/gen/libcarbon_hello_goodbye.a \
  -lthriftcpp2 \
  -lserverdbginfo \
  -ltransport \
  -lthriftanyrep \
  -lthrifttype \
  -lthrifttyperep \
  -lthriftprotocol \
  -lrpcmetadata \
  -lthriftannotation \
  -lthriftmetadata \
  -lasync \
  -lconcurrency \
  -lruntime \
  -lthrift-core \
  -lwangle \
  -lfizz \
  -lsodium \
  -lfolly
