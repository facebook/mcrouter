SUBDIRS = lib . routes test

ACLOCAL_AMFLAGS = -I m4

noinst_LIBRARIES = libmcroutercore.a
bin_PROGRAMS = mcrouter

BUILT_SOURCES = \
  lib/mc/ascii_client.c \
  lib/network/McAsciiParser-gen.cpp

lib/mc/ascii_client.c: lib/mc/ascii_client.rl
	ragel -G1 -o lib/mc/ascii_client.c lib/mc/ascii_client.rl

lib/network/McAsciiParser-gen.cpp: lib/network/McAsciiParser.rl
	ragel -G1 -o lib/network/McAsciiParser-gen.cpp lib/network/McAsciiParser.rl

CLEANFILES = \
  lib/mc/ascii_client.c \
  lib/network/McAsciiParser-gen.cpp

libmcroutercore_a_SOURCES = \
  async.cpp \
  async.h \
  awriter.h \
  CallbackPool-inl.h \
  CallbackPool.h \
  ClientPool.cpp \
  ClientPool.h \
  ConfigApi.cpp \
  ConfigApi.h \
  CyclesObserver.cpp \
  CyclesObserver.h \
  ExponentialSmoothData.h \
  FileDataProvider.cpp \
  FileDataProvider.h \
  FileObserver.cpp \
  FileObserver.h \
  flavor.cpp \
  flavor.h \
  LeaseTokenMap.cpp \
  LeaseTokenMap.h \
  mcrouter_config-impl.h \
  mcrouter_config.cpp \
  mcrouter_config.h \
  mcrouter_options_list.h \
  McrouterClient-inl.h \
  McrouterClient.cpp \
  McrouterClient.h \
  McrouterFiberContext.cpp \
  McrouterFiberContext.h \
  McrouterInstance.cpp \
  McrouterInstance.h \
  McrouterLogFailure.h \
  McrouterLogFailure.cpp \
  McrouterLogger.cpp \
  McrouterLogger.h \
  Observable-inl.h \
  Observable.h \
  options-template.h \
  options.cpp \
  options.h \
  OptionsUtil.cpp \
  OptionsUtil.h \
  PoolFactory.cpp \
  PoolFactory.h \
  proxy-inl.h \
  proxy.cpp \
  proxy.h \
  ProxyClientCommon.cpp \
  ProxyClientCommon.h \
  ProxyConfig.cpp \
  ProxyConfig.h \
  ProxyConfigBuilder.cpp \
  ProxyConfigBuilder.h \
  ProxyDestination-inl.h \
  ProxyDestination.cpp \
  ProxyDestination.h \
  ProxyDestinationMap.cpp \
  ProxyDestinationMap.h \
  ProxyRequestContext-inl.h \
  ProxyRequestContext.cpp \
  ProxyRequestContext.h \
  ProxyRequestLogger-inl.h \
  ProxyRequestLogger.h \
  ProxyThread.cpp \
  ProxyThread.h \
  route.cpp \
  route.h \
  routes/AllAsyncRoute.cpp \
  routes/AllFastestRoute.cpp \
  routes/AllInitialRoute.cpp \
  routes/AllMajorityRoute.cpp \
  routes/AllSyncRoute.cpp \
  routes/AsynclogRoute.cpp \
  routes/AsynclogRoute.h \
  routes/BigValueRoute-inl.h \
  routes/BigValueRoute.cpp \
  routes/BigValueRoute.h \
  routes/BigValueRouteIf.h \
  routes/DefaultShadowPolicy.h \
  routes/DestinationRoute.cpp \
  routes/DestinationRoute.h \
  routes/DevNullRoute.cpp \
  routes/DevNullRoute.h \
  routes/ErrorRoute.cpp \
  routes/ExtraRouteHandleProviderIf.h \
  routes/FailoverRateLimiter.cpp \
  routes/FailoverRateLimiter.h \
  routes/FailoverRoute.cpp \
  routes/FailoverRoute.h \
  routes/FailoverWithExptimeRoute.cpp \
  routes/HashRoute.cpp \
  routes/HostIdRoute.cpp \
  routes/L1L2CacheRoute.cpp \
  routes/LatestRoute.cpp \
  routes/LoggingRoute.cpp \
  routes/McExtraRouteHandleProvider.cpp \
  routes/McExtraRouteHandleProvider.h \
  routes/McImportResolver.cpp \
  routes/McImportResolver.h \
  routes/McOpList.h \
  routes/McRouteHandleBuilder.h \
  routes/McRouteHandleProvider.cpp \
  routes/McRouteHandleProvider.h \
  routes/McrouterRouteHandle.h \
  routes/MigrateRoute.cpp \
  routes/MissFailoverRoute.cpp \
  routes/MissFailoverRoute.h \
  routes/ModifyExptimeRoute.cpp \
  routes/ModifyExptimeRoute.h \
  routes/ModifyKeyRoute.cpp \
  routes/ModifyKeyRoute.h \
  routes/NullRoute.cpp \
  routes/OperationSelectorRoute.cpp \
  routes/OperationSelectorRoute.h \
  routes/OutstandingLimitRoute.cpp \
  routes/OutstandingLimitRoute.h \
  routes/PrefixSelectorRoute.cpp \
  routes/PrefixSelectorRoute.h \
  routes/ProxyRoute.cpp \
  routes/ProxyRoute.h \
  routes/RandomRoute.cpp \
  routes/RateLimiter.cpp \
  routes/RateLimiter.h \
  routes/RateLimitRoute.cpp \
  routes/RateLimitRoute.h \
  routes/RootRoute.h \
  routes/RouteHandleMap.cpp \
  routes/RouteHandleMap.h \
  routes/RoutePolicyMap.cpp \
  routes/RoutePolicyMap.h \
  routes/RouteSelectorMap.h \
  routes/ShadowRoute.cpp \
  routes/ShadowRoute.h \
  routes/ShadowRoute-inl.h \
  routes/ShadowRouteIf.h \
  routes/ShardHashFunc.cpp \
  routes/ShardHashFunc.h \
  routes/ShardSplitRoute.cpp \
  routes/ShardSplitRoute.h \
  routes/ShardSplitter.cpp \
  routes/ShardSplitter.h \
  routes/SlowWarmUpRoute.cpp \
  routes/SlowWarmupRoute.h \
  routes/SlowWarmUpRouteSettings.cpp \
  routes/SlowWarmupRouteSettings.h \
  routes/TimeProviderFunc.h \
  routes/WarmUpRoute.cpp \
  routes/WarmUpRoute.h \
  RoutingPrefix.cpp \
  RoutingPrefix.h \
  RuntimeVarsData.cpp \
  RuntimeVarsData.h \
  ServiceInfo.cpp \
  ServiceInfo.h \
  ShadowValidationData.h \
  stat_list.h \
  stats.cpp \
  stats.h \
  ThreadUtil.cpp \
  ThreadUtil.h \
  TkoCounters.h \
  TkoLog.cpp \
  TkoLog.h \
  TkoTracker.cpp \
  TkoTracker.h \
  TokenBucket.h

mcrouter_SOURCES = \
  main.cpp \
  server.cpp \
  server.h \
  ServerOnRequestImpl.h \
  ServerOnRequestCommon.h \
  standalone_options.h \
  standalone_options_list.h

libmcroutercore_a_CPPFLAGS = -I..

mcrouter_LDADD = libmcroutercore.a lib/libmcrouter.a
mcrouter_CPPFLAGS = -I..
