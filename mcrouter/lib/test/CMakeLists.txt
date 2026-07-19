include(GoogleTest)

add_executable(
  mcrouter_lib_test
  Main.cpp
  CompressionTestUtil.cpp
  HashTestUtil.cpp
  Ch3HashTest.cpp
  CompressionTest.cpp
  CompressionCodecManagerTest.cpp
  Crc32HashTest.cpp
  DynamicUtilTest.cpp
  FiberLocalTest.cpp
  IovecCursorTest.cpp
  Lz4ImmutableTest.cpp
  McResUtilTest.cpp
  MigrateRouteTest.cpp
  RandomRouteTest.cpp
  RendezvousHashTest.cpp
  RouteHandleTest.cpp
  WeightedCh3HashFuncTest.cpp
  WeightedChHashFuncBaseTest.cpp
  WeightedRendezvousHashTest.cpp
  observable_test.cpp
  runtime_vars_data_test.cpp
)

target_link_libraries(
  mcrouter_lib_test
  PRIVATE
    mcrouter_lib
    GTest::gtest
    GTest::gmock
)

gtest_discover_tests(mcrouter_lib_test)
