add_library(
  mcrouter_network_test_util
  STATIC
  ClientSocket.cpp
  ListenSocket.cpp
  MockMc.cpp
  SessionTestHarness.cpp
  TestClientServerUtil.cpp
  TestMcAsciiParserUtil.cpp
  gen/CarbonTestMessages.cpp
  gen/CarbonTestMessagesThrift.cpp
)

target_link_libraries(
  mcrouter_network_test_util
  PUBLIC
    mcrouter_lib
    mcrouter_carbon_test_thrift
    GTest::gtest
)

# =============================================================================
# Mock server executables (used for integration tests)
# =============================================================================
add_executable(mock_mc_server MockMcServer.cpp MockMc.cpp)
target_link_libraries(mock_mc_server PRIVATE mcrouter_lib)

add_executable(mock_mc_thrift_server MockMcThriftServer.cpp MockMc.cpp)
target_link_libraries(mock_mc_thrift_server PRIVATE mcrouter_lib)

add_executable(mock_mc_server_dual MockMcServerDual.cpp MockMc.cpp)
target_link_libraries(mock_mc_server_dual PRIVATE mcroutercore)

add_executable(
  mcrouter_network_test
  ${CMAKE_SOURCE_DIR}/mcrouter/lib/test/Main.cpp
  AccessPointTest.cpp
  AsyncMcClientTestSync.cpp
  AsyncMcServerShutdownTest.cpp
  AsyncMcServerTest.cpp
  CarbonMessageDispatcherTest.cpp
  CarbonMockMcTest.cpp
  CarbonQueueAppenderTest.cpp
  McAsciiParserTest.cpp
  McParserTest.cpp
  McServerAsciiParserTest.cpp
  MemcacheConnectionTest.cpp
  MemcacheTest.cpp
  RequestExpiryTest.cpp
  SessionTest.cpp
)

target_link_libraries(
  mcrouter_network_test
  PRIVATE
    mcrouter_network_test_util
    mcroutercore
    mcrouter_hello_goodbye
    GTest::gtest
    GTest::gmock
)

gtest_discover_tests(mcrouter_network_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
