Mcrouter installation
-----------------

## Automatic installation

**TODO!!!**: use script from **D1377312** (once it is committed).

## Manual installation

### Install

``` Shell
    # prime sudo
    sudo echo

    sudo yum install libopenssl-devel libcap-devel libevent-devel boost-devel \
    gtest-devel glog-devel gflags-devel snappy-devel zlib scons flex bison \
    krb5-devel binutils-devel
```

### Download and install

* latest autotools
Simplest way is to use this script:
http://git.savannah.gnu.org/cgit/coreutils.git/tree/scripts/autotools-install

* python 2.7

* folly
Follow instructions from https://github.com/facebook/folly

* fbthrift
  * Download fbthrift from https://github.com/facebook/fbthrift.
if you have errors regarding 'double-conversion' missing, add
*-ldouble-conversion* to AC_SUBST() macro in *thrift/configure.ac*
  * If you have *folly/String.h* not found errors,
add *-I/fbcode/folly* to CPPFLAGS:
``` Shell
 CPPFLAGS="-I~/fbcode/folly" ./configure
```
  * Build and install it. In *thrift* subfolder run
``` Shell
    autoreconf --install
    ./configure
    make
    sudo make install
```

### Build mcrouter

Clone mcrouter source code from https://github.com/facebook/mcrouter.
In mcrouter folder run

``` Shell
    autoreconf --install
    ./configure
    make
```

### Run unit tests (optional)

Run

``` Shell
    make check
```

if you have "symbol not found" errors from gtest, build it and put
libgtest.a/libgtest_main.a into you LD_LIBRARY_PATH. To build gtest:

* load it from http://googletest.googlecode.com/files/gtest-1.6.0.zip
* in make subfolder run
```
    make
```
* rename gtest.a to libgtest.a; gtest_main.a to libgtest_main.a
* don't forget to add libgtest.a and libgtestmain.a to your LD_LIBRARY_PATH
