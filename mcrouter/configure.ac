# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.62)

m4_define([mcrouter_version_str], m4_esyscmd_s([cut -d '-' -f 1 VERSION]))
m4_define([mcrouter_version_suffix], m4_esyscmd_s([sed 's/^[0-9]*:[0-9]*//' VERSION]))
m4_define([mcrouter_version], m4_translit(mcrouter_version_str, [:], [.]))
m4_append([mcrouter_version], [.0])
m4_append([mcrouter_version], [mcrouter_version_suffix])
AC_INIT([mcrouter], [mcrouter_version], mcrouter@fb.com)

LT_VERSION=mcrouter_version_str:0
AC_SUBST([LT_VERSION])

AC_CONFIG_SRCDIR([McrouterInstance.h])
AC_CONFIG_HEADERS([config.h:config.hin])
AC_CONFIG_LINKS([config-impl.h:mcrouter_config-impl.h])
AC_CONFIG_LINKS([lib/network/CaretSerializedMessage.h:lib/network/CaretSerializedMessageImpl.h])
AC_CONFIG_LINKS([lib/network/CaretReplyConverter.h:lib/network/CaretReplyConverterImpl.h])
AC_CONFIG_LINKS([ServerOnRequest.h:ServerOnRequestImpl.h])
AC_CONFIG_LINKS([test/config.py:test/mcrouter_config.py])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([1.11.1 foreign dist-bzip2 nostdinc subdir-objects parallel-tests])

AC_CONFIG_MACRO_DIR([m4])

AC_PROG_INSTALL
AC_PROG_LIBTOOL

AC_LANG([C++])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AX_CXX_COMPILE_STDCXX_11([],[mandatory])
LT_INIT

CXXFLAGS="-fno-strict-aliasing $CXXFLAGS"
CXXFLAGS="-W -Wall -Wextra -Wno-unused-parameter $CXXFLAGS"
CXXFLAGS=" -Wno-missing-field-initializers -Wno-deprecated $CXXFLAGS"
CXXFLAGS="-DLIBMC_FBTRACE_DISABLE $CXXFLAGS"

CFLAGS="-DLIBMC_FBTRACE_DISABLE $CFLAGS"

# Checks for glog and gflags
# There are no symbols with C linkage, so we do a try-run
AC_HAVE_LIBRARY([glog],[],[AC_MSG_ERROR(
                [Please install google-glog library])])
AC_CACHE_CHECK(
  [for glog viability],
  [mcrouter_cv_prog_cc_glog],
  [AC_RUN_IFELSE(
    [AC_LANG_SOURCE[
      #include <glog/logging.h>
      int main(int argc, char** argv) {
        google::InitGoogleLogging(argv[0]);
        google::ShutdownGoogleLogging();
        return 0;
      }
    ]],
    [mcrouter_cv_prog_cc_glog=yes],
    [mcrouter_cv_prog_cc_glog=no]
  )]
)

if test "$mcrouter_cv_prog_cc_glog" != "yes"; then
  AC_MSG_ERROR(["libglog invalid, see config.log for details"])
fi

AC_HAVE_LIBRARY([gflags],[],[AC_MSG_ERROR(
                [Please install google-gflags library])])
AC_CACHE_CHECK(
  [for gflags viability],
  [mcrouter_cv_prog_cc_gflags],
  [AC_RUN_IFELSE(
    [AC_LANG_SOURCE[
      #include <gflags/gflags.h>
      DEFINE_bool(folly_truthy, true, "Sample truthy flag");
      DEFINE_bool(folly_falsey, false, "Sample falsey flag");
      int main(int argc, char** argv) {
        return (FLAGS_folly_truthy && !FLAGS_folly_falsey) ? 0 : 1;
      }
    ]],
    [mcrouter_cv_prog_cc_gflags=yes],
    [mcrouter_cv_prog_cc_gflags=no]
  )]
)

if test "$mcrouter_cv_prog_cc_gflags" != "yes"; then
  AC_MSG_ERROR(["libgflags invalid, see config.log for details"])
fi

# check for boost libs
AX_BOOST_BASE([1.51.0], [], [AC_MSG_ERROR(
              [Please install boost >= 1.51.0 (context, regex, filesystem and system)])])
# TODO: also check boost::ctx, not only boost::context
AX_BOOST_CONTEXT
AX_BOOST_REGEX
AX_BOOST_SYSTEM
AX_BOOST_FILESYSTEM

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h stdint.h string.h sys/file.h sys/time.h syslog.h unistd.h malloc.h])

AC_CHECK_LIB([event], [event_set], [], [AC_MSG_ERROR([Unable to find libevent])])
AC_CHECK_LIB([crypto], [MD5_Init], [], [AC_MSG_ERROR([Unable to find libcrypto])])
AC_CHECK_LIB([ssl], [SSL_library_init], [], [AC_MSG_ERROR([Unable to find ssl])])
AC_CHECK_LIB([z], [gzread], [], [AC_MSG_ERROR([Unable to find zlib])])
AC_CHECK_LIB([double-conversion],[ceil],[],[AC_MSG_ERROR(
             [Please install double-conversion library])])
AC_CHECK_LIB([folly],[getenv],[],[AC_MSG_ERROR(
             [Please install the folly library])])
AC_CHECK_HEADER([folly/Likely.h], [], [AC_MSG_ERROR(
[Couldn't find folly, please download from https://github.com/facebook/folly]
)], [])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_C_VOLATILE
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

AC_CHECK_TYPE([__int128],
  [AC_DEFINE([HAVE_INT128_T], [1], [Define if __int128 exists])],
  [AC_DEFINE([HAVE_INT128_T], [0], [Define if __int128 does not exist])])
AC_CHECK_TYPES([ptrdiff_t])
AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE[
    #pragma GCC diagnostic error "-Wattributes"
    extern "C" void (*test_ifunc(void))() { return 0; }
    void func() __attribute__((ifunc("test_ifunc")));]
  ],
  [AC_DEFINE([HAVE_IFUNC], [1], [Define to 1 if the compiler supports ifunc])],
  [AC_DEFINE([HAVE_IFUNC], [0], [Define to 0 if the compiler doesn't support ifunc])]
)
AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE[class C { virtual void f() final {} virtual void g() {} };
                  class D : public C { virtual void g() override {} };]],
  [AC_DEFINE([FINAL], [final],
             [Define to "final" if the compiler supports C++11 "final"])
   AC_DEFINE([OVERRIDE], [override],
             [Define to "override" if the compiler supports C++11 "override"])],
  [AC_DEFINE([FINAL], [],
             [Define to "final" if the compiler supports C++11 "final"])
   AC_DEFINE([OVERRIDE], [],
             [Define to "override" if the compiler supports C++11 "override"])]
)

AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE[
    #include <thread>
    #include <chrono>
    void func() { std::this_thread::sleep_for(std::chrono::seconds(1)); }]],
  [AC_DEFINE([HAVE_STD__THIS_THREAD__SLEEP_FOR], [1],
             [Define to 1 if std::this_thread::sleep_for() is defined.])])

AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE[
    #include <cstring>
    static constexpr int val = strlen("foo");]],
  [AC_DEFINE([HAVE_CONSTEXPR_STRLEN], [1],
             [Define to 1 if strlen(3) is constexpr.])])

AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE[
    #include <type_traits>
    #if !_LIBCPP_VERSION
    #error No libc++
    #endif
    void func() {}]
  ],
  [AC_DEFINE([USE_LIBCPP], [1], [Define to 1 if we're using libc++.])])

AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE[
    #include <type_traits>
    const bool val = std::is_trivially_copyable<bool>::value;]
  ],
  [AC_DEFINE([HAVE_STD__IS_TRIVIALLY_COPYABLE], [1],
             [Define to 1 if we have a usable std::is_trivially_copyable<T>
              implementation.])])

# Figure out if we support weak symbols. If not, we will link in some null
# stubs for functions that would otherwise be weak.
AC_LINK_IFELSE(
  [AC_LANG_SOURCE[
    extern "C" void configure_link_extern_weak_test() __attribute__((weak));
    int main(int argc, char** argv) {
        return configure_link_extern_weak_test == nullptr;
    }]
  ],
  [
    ac_have_weak_symbols="yes"
    AC_DEFINE([HAVE_WEAK_SYMBOLS], [1],
              [Define to 1 if the linker supports weak symbols.])])

AC_SEARCH_LIBS([cplus_demangle_v3_callback], [iberty])
if test "$ac_cv_search_cplus_demangle_v3_callback" != "no" ; then
  AC_DEFINE([HAVE_CPLUS_DEMANGLE_V3_CALLBACK], [1],
            [Define to 1 if we have cplus_demangle_v3_callback.])
fi

# Check for clock_gettime(2). This is not in an AC_CHECK_FUNCS() because we
# want to link with librt if necessary.
AC_SEARCH_LIBS([clock_gettime], [rt],
  AC_DEFINE(
    [HAVE_CLOCK_GETTIME],
    [1],
    [Define to 1 if we support clock_gettime(2).]),
  [])

# Checks for library functions.
AC_CHECK_FUNCS([gettimeofday \
                localtime_r \
                memchr \
                memset \
                mkdir \
                socket \
                strcasecmp \
                strdup \
                strerror \
                strtol \
                dup2 \
                ftruncate])

LIBS="$LIBS $BOOST_LDFLAGS $BOOST_CONTEXT_LIB $BOOST_FILESYSTEM_LIB \
     $BOOST_SYSTEM_LIB $BOOST_REGEX_LIB -lpthread -pthread"

AM_CONDITIONAL([HAVE_STD_THREAD], [test "$ac_cv_header_features" = "yes"])
AM_CONDITIONAL([HAVE_X86_64], [test "$build_cpu" = "x86_64"])
AM_CONDITIONAL([HAVE_LINUX], [test "$build_os" == "linux-gnu"])
AM_CONDITIONAL([HAVE_WEAK_SYMBOLS], [test "$ac_have_weak_symbols" = "yes"])
AM_CONDITIONAL([HAVE_BITS_FUNCTEXCEPT], [test "$ac_cv_header_bits_functexcept" = "yes"])

AM_PATH_PYTHON([2.6],, [:])
AM_CONDITIONAL([HAVE_PYTHON], [test "$PYTHON" != :])

AH_BOTTOM([#include "mcrouter_config.h"])

# Output
AC_CONFIG_FILES([Makefile
                 lib/Makefile
                 lib/test/Makefile
                 lib/config/Makefile
                 lib/config/test/Makefile
                 lib/cycles/Makefile
                 lib/cycles/test/Makefile
                 lib/fbi/Makefile
                 lib/fbi/test/Makefile
                 lib/fbi/cpp/Makefile
                 lib/fbi/cpp/test/Makefile
                 lib/mc/Makefile
                 lib/mc/test/Makefile
                 lib/network/Makefile
                 lib/network/test/Makefile
                 routes/Makefile
                 routes/test/Makefile
                 test/Makefile
                 test/cpp_unit_tests/Makefile])

AC_OUTPUT
