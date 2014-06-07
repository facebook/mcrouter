#!/bin/bash

cd "$1"
./mcrouter/test/cpp_unit_tests/mcrouter_libmc_test
./mcrouter/test/cpp_unit_tests/mcrouter_test
./mcrouter/lib/config/test/mcrouter_config_test
./mcrouter/lib/fbi/test/mcrouter_fbi_test
./mcrouter/lib/fbi/cpp/test/mcrouter_fbi_cpp_test
./mcrouter/lib/fibers/test/mcrouter_fibers_test
./mcrouter/lib/network/test/mcrouter_network_test
./mcrouter/lib/test/mcrouter_lib_test
./mcrouter/routes/test/mcrouter_routes_test

python -B -m unittest discover -s ./mcrouter/test -p 'test_*.py'
