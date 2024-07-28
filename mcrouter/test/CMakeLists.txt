# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

if(BUILD_TESTS)
  find_package(Python3 COMPONENTS Interpreter)

  # Python integration tests need to run from the build directory so that they
  # can access mcrouter binaries, so copy them and their fixtures over.
  file(GLOB mcrouter_integration_test_files CONFIGURE_DEPENDS "*.json" "*.py")
  foreach(TEST_FILE ${mcrouter_integration_test_files})
    configure_file(${TEST_FILE} ${CMAKE_CURRENT_BINARY_DIR} COPYONLY)
  endforeach()

  configure_file(mcrouter_config.py ${CMAKE_CURRENT_BINARY_DIR}/config.py
                 COPYONLY)

  file(GLOB mcrouter_integration_test_cases CONFIGURE_DEPENDS "test_*.py")
  foreach(TEST_CASE ${mcrouter_integration_test_cases})
    get_filename_component(test_name ${TEST_CASE} NAME_WE)
    add_test(
      NAME "integration_${test_name}"
      COMMAND ${Python3_EXECUTABLE} -B -m unittest -q mcrouter.test.${test_name}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties("integration_${test_name}" PROPERTIES TIMEOUT 300)
  endforeach()
endif()

add_subdirectory(cpp_unit_tests)
