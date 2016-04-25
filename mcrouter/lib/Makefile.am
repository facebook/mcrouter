SUBDIRS = . allocator config cycles fbi mc network test

noinst_LIBRARIES = libmcrouter.a

BUILT_SOURCES = \
  network/gen-cpp2/mc_caret_protocol_types.cpp \
  network/gen-cpp2/mc_caret_protocol_types.h \
  network/gen-cpp2/mc_caret_protocol_types_custom_protocol.h

# The `sed` hack below is necessary until the Thrift compiler is modified
# to not generate service-related includes when linking only against
# libthriftprotocol.
network/gen-cpp2/mc_caret_protocol_types.cpp: network/gen-cpp2/mc_caret_protocol_types.h
network/gen-cpp2/mc_caret_protocol_types_custom_protocol.h: network/gen-cpp2/mc_caret_protocol_types.h
network/gen-cpp2/mc_caret_protocol_types.h: network/mc_caret_protocol.thrift
	PYTHONPATH=$(THRIFT2_COMP_DIR)/..:$$PYTHONPATH $(THRIFT) --gen cpp2 -o network/ $<
	find network/gen-cpp2/ -name 'mc_caret_protocol*' -type f | xargs sed -i 's/^.*GeneratedCodeHelper\.h.*$$//g'
	find network/gen-cpp2/ -name 'mc_caret_protocol*' -type f | xargs sed -i 's/^.*Cpp2ConnContext\.h.*$$//g'

libmcrouter_a_SOURCES = \
  CacheClientStats.h \
  Ch3HashFunc.h \
  Compression.cpp \
  Compression.h \
  Crc32HashFunc.h \
  FailoverErrorsSettings.cpp \
  FailoverErrorsSettings.h \
  IOBufUtil.cpp \
  IOBufUtil.h \
  Keys.cpp \
  Keys.h \
  Lz4Immutable.cpp \
  Lz4Immutable.h \
  MessageStorage.h \
  MessageQueue.cpp \
  MessageQueue.h \
  McMsgRef.h \
  McOpList.h \
  McOperation.h \
  McOperationTraits.h \
  McReply-inl.h \
  McReply.cpp \
  McReply.h \
  McRequest.cpp \
  McRequest.h \
  McRequestList.h \
  McResUtil.h \
  Operation.h \
  OperationTraits.h \
  Reply.h \
  RouteHandleIf.h \
  RouteHandleTraverser.h \
  StatsReply.cpp \
  StatsReply.h \
  WeightedCh3HashFunc.cpp \
  WeightedCh3HashFunc.h \
  allocator/JemallocNodumpAllocator.h \
  allocator/JemallocNodumpAllocator.cpp \
  config/ConfigPreprocessor.cpp \
  config/ConfigPreprocessor.h \
  config/ImportResolverIf.h \
  config/RendezvousHash.cpp \
  config/RendezvousHash.h \
  config/RouteHandleBuilder.h \
  config/RouteHandleFactory-inl.h \
  config/RouteHandleFactory.h \
  config/RouteHandleProviderIf.h \
  cycles/Accumulator.cpp \
  cycles/Accumulator.h \
  cycles/Clocks.cpp \
  cycles/Clocks.h \
  cycles/Cycles.cpp \
  cycles/Cycles.h \
  cycles/ExtractorThread.cpp \
  cycles/ExtractorThread.h \
  cycles/Interval.h \
  debug/Fifo.cpp \
  debug/Fifo.h \
  debug/FifoManager.cpp \
  debug/FifoManager.h \
  fbi/asox_semaphore.c \
  fbi/asox_semaphore.h \
  fbi/asox_timer.c \
  fbi/asox_timer.h \
  fbi/counting_sem.c \
  fbi/counting_sem.h \
  fbi/cpp/FuncGenerator.h \
  fbi/cpp/LogFailure.cpp \
  fbi/cpp/LogFailure.h \
  fbi/cpp/Trie-inl.h \
  fbi/cpp/Trie.h \
  fbi/cpp/TypeList.h \
  fbi/cpp/globals.cpp \
  fbi/cpp/globals.h \
  fbi/cpp/ParsingUtil.cpp \
  fbi/cpp/ParsingUtil.h \
  fbi/cpp/sfrlock.h \
  fbi/cpp/util.cpp \
  fbi/cpp/util.h \
  fbi/debug.c \
  fbi/debug.h \
  fbi/decls.h \
  fbi/hash.c \
  fbi/hash.h \
  fbi/network.c \
  fbi/network.h \
  fbi/nstring.c \
  fbi/nstring.h \
  fbi/queue.h \
  fbi/sfrlock.c \
  fbi/sfrlock.h \
  fbi/time.h \
  fbi/util.h \
  mc/ascii_client.c \
  mc/ascii_client.h \
  mc/ascii_response.c \
  mc/ascii_response.h \
  mc/msg.c \
  mc/msg.h \
  mc/parser.c \
  mc/parser.h \
  mc/protocol.h \
  mc/umbrella.c \
  mc/umbrella.h \
  mc/umbrella_conv.h \
  mc/umbrella_protocol.c \
  mc/umbrella_protocol.h \
  mc/util.h \
  network/AccessPoint.cpp \
  network/AccessPoint.h \
  network/AsciiSerialized-inl.h \
  network/AsciiSerialized.cpp \
  network/AsciiSerialized.h \
  network/AsyncMcClient-inl.h \
  network/AsyncMcClient.h \
  network/AsyncMcClientImpl-inl.h \
  network/AsyncMcClientImpl.cpp \
  network/AsyncMcClientImpl.h \
  network/AsyncMcServer.cpp \
  network/AsyncMcServer.h \
  network/AsyncMcServerWorker.cpp \
  network/AsyncMcServerWorker.h \
  network/AsyncMcServerWorkerOptions.h \
  network/CaretReplyConverterImpl.cpp \
  network/CaretReplyConverterImpl.h \
  network/CaretSerializedMessageImpl-inl.h \
  network/CaretSerializedMessageImpl.h \
  network/ClientMcParser-inl.h \
  network/ClientMcParser.h \
  network/ConnectionOptions.h \
  network/ConnectionTracker.cpp \
  network/ConnectionTracker.h \
  network/gen-cpp2/mc_caret_protocol_types.cpp \
  network/gen-cpp2/mc_caret_protocol_types.h \
  network/McAsciiParser-gen.cpp \
  network/McAsciiParser-inl.h \
  network/McAsciiParser.cpp \
  network/McAsciiParser.h \
  network/McClientRequestContext-inl.h \
  network/McClientRequestContext.cpp \
  network/McClientRequestContext.h \
  network/McParser.cpp \
  network/McParser.h \
  network/McQueueAppender.cpp \
  network/McQueueAppender.h \
  network/McReplyToTypedConverter.cpp \
  network/McReplyToTypedConverter.h \
  network/McRequestToTypedConverter.cpp \
  network/McRequestToTypedConverter.h \
  network/McSerializedRequest.cpp \
  network/McSerializedRequest.h \
  network/McServerRequestContext-inl.h \
  network/McServerRequestContext.cpp \
  network/McServerRequestContext.h \
  network/McServerSession.cpp \
  network/McServerSession.h \
  network/MockMcClientTransport.cpp \
  network/MockMcClientTransport.h \
  network/MultiOpParent.cpp \
  network/MultiOpParent.h \
  network/ServerMcParser-inl.h \
  network/ServerMcParser.h \
  network/ThreadLocalSSLContextProvider.cpp \
  network/ThreadLocalSSLContextProvider.h \
  network/ThriftMessageList.h \
  network/ThriftMsgDispatcher.h \
  network/TypedToMcRequestConverter-inl.h \
  network/TypedToMcRequestConverter.cpp \
  network/TypedToMcRequestConverter.h \
  network/UmbrellaProtocol.cpp \
  network/UmbrellaProtocol.h \
  network/UniqueIntrusiveList.h \
  network/WriteBuffer.cpp \
  network/WriteBuffer.h \
  routes/AllAsyncRoute.h \
  routes/AllFastestRoute.h \
  routes/AllInitialRoute.h \
  routes/AllMajorityRoute.h \
  routes/AllSyncRoute.h \
  routes/ErrorRoute.h \
  routes/HashRoute.h \
  routes/LoggingRoute.h \
  routes/MigrateRoute.h \
  routes/NullRoute.h \
  routes/RandomRoute.h

libmcrouter_a_CPPFLAGS = -I$(top_srcdir)/..
libmcrouter_a_CFLAGS = -I$(top_srcdir)/..

# build gtest on check
check_LTLIBRARIES = libgtest.la libgtestmain.la
check_PROGRAMS =

libgtest_la_CPPFLAGS = -Igtest/include -Igtest
libgtest_la_SOURCES = gtest/src/gtest-all.cc

libgtestmain_la_CPPFLAGS = -Igtest/include -Igtest
libgtestmain_la_SOURCES = gtest/src/gtest_main.cc
libgtestmain_la_LIBADD = libgtest.la
