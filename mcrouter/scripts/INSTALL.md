N.B These scripts *only* support Amazon Linux 2

1. Install compilers and system-provided libraries:

        ./install_deps_amazon-linux-2.sh

2. Compile mcrouter and dependencies

        MY_INSTALL_DIR=path/to/install/dir
        TARGET=mcrouter
        ./get_and_build_by_make $MY_INSTALL_DIR mcrouter

You can substitute individual dependencies as the value of TARGET in order to debug. 
