## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

# We use serial-tests so that test output will be written directly to stdout
# which is much preferred in CI environments where the test logs may be hard
# to get at after the fact. Most of our tests are bundled into a single
# executable anyway so cannot easily be parallelized.
AUTOMAKE_OPTIONS = foreign subdir-objects serial-tests

# When running distcheck, verify that we've included all the files needed by
# the cmake build.
distcheck-hook:
	rm -rf distcheck-cmake
	(mkdir distcheck-cmake && cd distcheck-cmake && cmake ../$(distdir) && make -j6 check)
	rm -rf distcheck-cmake

AM_CXXFLAGS = -I$(srcdir)/src -I$(builddir)/src -DKJ_HEADER_WARNINGS -DCAPNP_HEADER_WARNINGS -DCAPNP_INCLUDE_DIR='"$(includedir)"' $(PTHREAD_CFLAGS)

AM_LDFLAGS = $(PTHREAD_CFLAGS)

EXTRA_DIST =                                                                 \
  README.txt                                                                 \
  LICENSE.txt                                                                \
  $(test_capnpc_inputs)                                                      \
  src/capnp/compiler/capnp-test.sh                                           \
  src/capnp/testdata/segmented-packed                                        \
  src/capnp/testdata/errors.capnp.nobuild                                    \
  src/capnp/testdata/errors2.capnp.nobuild                                   \
  src/capnp/testdata/no-file-id.capnp.nobuild                                \
  src/capnp/testdata/short.txt                                               \
  src/capnp/testdata/flat                                                    \
  src/capnp/testdata/binary                                                  \
  src/capnp/testdata/errors.txt                                              \
  src/capnp/testdata/errors2.txt                                             \
  src/capnp/testdata/segmented                                               \
  src/capnp/testdata/packed                                                  \
  src/capnp/testdata/pretty.txt                                              \
  src/capnp/testdata/lists.binary                                            \
  src/capnp/testdata/packedflat                                              \
  src/capnp/testdata/pretty.json                                             \
  src/capnp/testdata/short.json                                              \
  src/capnp/testdata/annotated-json.binary                                   \
  src/capnp/testdata/annotated.json                                          \
  CMakeLists.txt                                                             \
  cmake/CapnProtoMacros.cmake                                                \
  cmake/CapnProtoTargets.cmake                                               \
  cmake/CapnProtoConfig.cmake.in                                             \
  cmake/CapnProtoConfigVersion.cmake.in                                      \
  src/CMakeLists.txt                                                         \
  src/kj/CMakeLists.txt                                                      \
  src/capnp/CMakeLists.txt

CLEANFILES = $(test_capnpc_outputs) test_capnpc_middleman distcheck-cmake

# Deletes all the files generated by autoreconf.
MAINTAINERCLEANFILES =   \
  aclocal.m4             \
  config.guess           \
  config.sub             \
  configure              \
  depcomp                \
  install-sh             \
  ltmain.sh              \
  Makefile.in            \
  missing                \
  mkinstalldirs          \
  config.h.in            \
  stamp.h.in             \
  m4/ltsugar.m4          \
  m4/libtool.m4          \
  m4/ltversion.m4        \
  m4/lt~obsolete.m4      \
  m4/ltoptions.m4

maintainer-clean-local:
	-rm -rf build-aux

# gmake defines an implicit rule building n from n.o.  Unfortunately, this triggers on our .capnp
# files because they generate .capnp.c++ which is compiled to .capnp.o.  In addition to being
# nonsense, this leads to cyclic dependency issues and could even cause the .capnp files to be
# unexpectedly overwritten!  We need to cancel the implicit rule by declaring an explicit one.
#
# I want the hours of my life back that I spent figuring this out.
%.capnp:
	@:

public_capnpc_inputs =                                         \
  src/capnp/c++.capnp                                          \
  src/capnp/schema.capnp                                       \
  src/capnp/stream.capnp                                       \
  src/capnp/rpc.capnp                                          \
  src/capnp/rpc-twoparty.capnp                                 \
  src/capnp/persistent.capnp

capnpc_inputs =                                                \
  $(public_capnpc_inputs)                                      \
  src/capnp/compiler/lexer.capnp                               \
  src/capnp/compiler/grammar.capnp                             \
  src/capnp/compat/json.capnp

capnpc_outputs =                                               \
  src/capnp/c++.capnp.c++                                      \
  src/capnp/c++.capnp.h                                        \
  src/capnp/schema.capnp.c++                                   \
  src/capnp/schema.capnp.h                                     \
  src/capnp/stream.capnp.c++                                   \
  src/capnp/stream.capnp.h                                     \
  src/capnp/rpc.capnp.c++                                      \
  src/capnp/rpc.capnp.h                                        \
  src/capnp/rpc-twoparty.capnp.c++                             \
  src/capnp/rpc-twoparty.capnp.h                               \
  src/capnp/persistent.capnp.c++                               \
  src/capnp/persistent.capnp.h                                 \
  src/capnp/compat/json.capnp.h                                \
  src/capnp/compat/json.capnp.c++                              \
  src/capnp/compiler/lexer.capnp.c++                           \
  src/capnp/compiler/lexer.capnp.h                             \
  src/capnp/compiler/grammar.capnp.c++                         \
  src/capnp/compiler/grammar.capnp.h

includecapnpdir = $(includedir)/capnp
includecapnpcompatdir = $(includecapnpdir)/compat
includekjdir = $(includedir)/kj
includekjparsedir = $(includekjdir)/parse
includekjstddir = $(includekjdir)/std
includekjcompatdir = $(includekjdir)/compat

dist_includecapnp_DATA = $(public_capnpc_inputs)
dist_includecapnpcompat_DATA = src/capnp/compat/json.capnp

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(CAPNP_PKG_CONFIG_FILES)

cmakeconfigdir = $(libdir)/cmake/CapnProto
cmakeconfig_DATA = $(CAPNP_CMAKE_CONFIG_FILES)                 \
  cmake/CapnProtoMacros.cmake                                  \
  cmake/CapnProtoTargets.cmake

noinst_HEADERS = \
  src/kj/miniposix.h \
  src/kj/async-io-internal.h

includekj_HEADERS =                                            \
  src/kj/common.h                                              \
  src/kj/units.h                                               \
  src/kj/memory.h                                              \
  src/kj/refcount.h                                            \
  src/kj/array.h                                               \
  src/kj/list.h                                                \
  src/kj/vector.h                                              \
  src/kj/string.h                                              \
  src/kj/string-tree.h                                         \
  src/kj/glob-filter.h                                         \
  src/kj/hash.h                                                \
  src/kj/table.h                                               \
  src/kj/map.h                                                 \
  src/kj/encoding.h                                            \
  src/kj/exception.h                                           \
  src/kj/debug.h                                               \
  src/kj/arena.h                                               \
  src/kj/io.h                                                  \
  src/kj/tuple.h                                               \
  src/kj/one-of.h                                              \
  src/kj/function.h                                            \
  src/kj/mutex.h                                               \
  src/kj/source-location.h                                     \
  src/kj/thread.h                                              \
  src/kj/filesystem.h                                          \
  src/kj/async-prelude.h                                       \
  src/kj/async.h                                               \
  src/kj/async-inl.h                                           \
  src/kj/time.h                                                \
  src/kj/timer.h                                               \
  src/kj/async-unix.h                                          \
  src/kj/async-win32.h                                         \
  src/kj/async-io.h                                            \
  src/kj/cidr.h                                                \
  src/kj/async-queue.h                                         \
  src/kj/main.h                                                \
  src/kj/test.h                                                \
  src/kj/win32-api-version.h                                   \
  src/kj/windows-sanity.h

includekjparse_HEADERS =                                       \
  src/kj/parse/common.h                                        \
  src/kj/parse/char.h

includekjstd_HEADERS =                                         \
  src/kj/std/iostream.h

includekjcompat_HEADERS =                                      \
  src/kj/compat/gtest.h                                        \
  src/kj/compat/url.h                                          \
  src/kj/compat/http.h                                         \
  src/kj/compat/gzip.h                                         \
  src/kj/compat/readiness-io.h                                 \
  src/kj/compat/tls.h

includecapnp_HEADERS =                                         \
  src/capnp/c++.capnp.h                                        \
  src/capnp/common.h                                           \
  src/capnp/blob.h                                             \
  src/capnp/endian.h                                           \
  src/capnp/layout.h                                           \
  src/capnp/orphan.h                                           \
  src/capnp/list.h                                             \
  src/capnp/any.h                                              \
  src/capnp/message.h                                          \
  src/capnp/capability.h                                       \
  src/capnp/membrane.h                                         \
  src/capnp/schema.capnp.h                                     \
  src/capnp/stream.capnp.h                                     \
  src/capnp/schema-lite.h                                      \
  src/capnp/schema.h                                           \
  src/capnp/schema-loader.h                                    \
  src/capnp/schema-parser.h                                    \
  src/capnp/dynamic.h                                          \
  src/capnp/pretty-print.h                                     \
  src/capnp/serialize.h                                        \
  src/capnp/serialize-async.h                                  \
  src/capnp/serialize-packed.h                                 \
  src/capnp/serialize-text.h                                   \
  src/capnp/pointer-helpers.h                                  \
  src/capnp/generated-header-support.h                         \
  src/capnp/raw-schema.h                                       \
  src/capnp/rpc-prelude.h                                      \
  src/capnp/rpc.h                                              \
  src/capnp/rpc-twoparty.h                                     \
  src/capnp/rpc.capnp.h                                        \
  src/capnp/rpc-twoparty.capnp.h                               \
  src/capnp/persistent.capnp.h

includecapnpcompat_HEADERS =                                   \
  src/capnp/compat/json.h                                      \
  src/capnp/compat/json.capnp.h                                \
  src/capnp/compat/std-iterator.h                              \
  src/capnp/compat/websocket-rpc.h

if BUILD_KJ_TLS
MAYBE_KJ_TLS_LA=libkj-tls.la
MAYBE_KJ_TLS_TESTS=                                            \
  src/kj/compat/readiness-io-test.c++                          \
  src/kj/compat/tls-test.c++
else
MAYBE_KJ_TLS_LA=
MAYBE_KJ_TLS_TESTS=
endif

if BUILD_KJ_GZIP
MAYBE_KJ_GZIP_LA=libkj-gzip.la
MAYBE_KJ_GZIP_TESTS=                                           \
  src/kj/compat/gzip-test.c++
else
MAYBE_KJ_TLS_LA=
MAYBE_KJ_TLS_TESTS=
endif

if LITE_MODE
lib_LTLIBRARIES = libkj.la libkj-test.la libcapnp.la
else
lib_LTLIBRARIES = libkj.la libkj-test.la libkj-async.la libkj-http.la $(MAYBE_KJ_TLS_LA) $(MAYBE_KJ_GZIP_LA) libcapnp.la libcapnp-rpc.la libcapnp-json.la libcapnp-websocket.la libcapnpc.la
endif

libkj_la_LIBADD = $(PTHREAD_LIBS)
libkj_la_LDFLAGS = -release $(SO_VERSION) -no-undefined
libkj_la_SOURCES=                                              \
  src/kj/cidr.c++                                              \
  src/kj/common.c++                                            \
  src/kj/units.c++                                             \
  src/kj/memory.c++                                            \
  src/kj/refcount.c++                                          \
  src/kj/array.c++                                             \
  src/kj/list.c++                                              \
  src/kj/string.c++                                            \
  src/kj/string-tree.c++                                       \
  src/kj/source-location.c++                                   \
  src/kj/glob-filter.c++                                       \
  src/kj/hash.c++                                              \
  src/kj/table.c++                                             \
  src/kj/encoding.c++                                          \
  src/kj/exception.c++                                         \
  src/kj/debug.c++                                             \
  src/kj/arena.c++                                             \
  src/kj/io.c++                                                \
  src/kj/mutex.c++                                             \
  src/kj/thread.c++                                            \
  src/kj/time.c++                                              \
  src/kj/filesystem.c++                                        \
  src/kj/filesystem-disk-unix.c++                                   \
  src/kj/filesystem-disk-win32.c++                             \
  src/kj/test-helpers.c++                                      \
  src/kj/main.c++                                              \
  src/kj/parse/char.c++

libkj_test_la_LIBADD = libkj.la $(PTHREAD_LIBS)
libkj_test_la_LDFLAGS = -release $(VERSION) -no-undefined
libkj_test_la_SOURCES = src/kj/test.c++

if !LITE_MODE
libkj_async_la_LIBADD = libkj.la $(ASYNC_LIBS) $(PTHREAD_LIBS)
libkj_async_la_LDFLAGS = -release $(SO_VERSION) -no-undefined
libkj_async_la_SOURCES=                                        \
  src/kj/async.c++                                             \
  src/kj/async-unix.c++                                        \
  src/kj/async-win32.c++                                       \
  src/kj/async-io.c++                                          \
  src/kj/async-io-unix.c++                                     \
  src/kj/async-io-win32.c++                                    \
  src/kj/timer.c++

if BUILD_KJ_GZIP
libkj_http_la_LIBADD = libkj-async.la libkj.la -lz $(ASYNC_LIBS) $(PTHREAD_LIBS)
libkj_http_la_LDFLAGS = -release $(SO_VERSION) -no-undefined
libkj_http_la_SOURCES=                                         \
  src/kj/compat/url.c++                                        \
  src/kj/compat/http.c++
else
libkj_http_la_LIBADD = libkj-async.la libkj.la $(ASYNC_LIBS) $(PTHREAD_LIBS)
libkj_http_la_LDFLAGS = -release $(SO_VERSION) -no-undefined
libkj_http_la_SOURCES=                                         \
  src/kj/compat/url.c++                                        \
  src/kj/compat/http.c++
endif

libkj_tls_la_LIBADD = libkj-async.la libkj.la -lssl -lcrypto $(ASYNC_LIBS) $(PTHREAD_LIBS)
libkj_tls_la_LDFLAGS = -release $(SO_VERSION) -no-undefined
libkj_tls_la_SOURCES=                                          \
  src/kj/compat/readiness-io.c++                               \
  src/kj/compat/tls.c++

libkj_gzip_la_LIBADD = libkj-async.la libkj.la -lz $(ASYNC_LIBS) $(PTHREAD_LIBS)
libkj_gzip_la_LDFLAGS = -release $(SO_VERSION) -no-undefined
libkj_gzip_la_SOURCES=                                          \
  src/kj/compat/gzip.c++

endif !LITE_MODE

if !LITE_MODE
heavy_sources =                                                \
  src/capnp/schema.c++                                         \
  src/capnp/schema-loader.c++                                  \
  src/capnp/dynamic.c++                                        \
  src/capnp/stringify.c++
endif !LITE_MODE

libcapnp_la_LIBADD = libkj.la $(PTHREAD_LIBS)
libcapnp_la_LDFLAGS = -release $(SO_VERSION) -no-undefined
libcapnp_la_SOURCES=                                           \
  src/capnp/c++.capnp.c++                                      \
  src/capnp/blob.c++                                           \
  src/capnp/arena.h                                            \
  src/capnp/arena.c++                                          \
  src/capnp/layout.c++                                         \
  src/capnp/list.c++                                           \
  src/capnp/any.c++                                            \
  src/capnp/message.c++                                        \
  src/capnp/schema.capnp.c++                                   \
  src/capnp/stream.capnp.c++                                   \
  src/capnp/serialize.c++                                      \
  src/capnp/serialize-packed.c++                               \
  $(heavy_sources)

if !LITE_MODE

libcapnp_rpc_la_LIBADD = libcapnp.la libkj-async.la libkj.la $(ASYNC_LIBS) $(PTHREAD_LIBS)
libcapnp_rpc_la_LDFLAGS = -release $(SO_VERSION) -no-undefined
libcapnp_rpc_la_SOURCES=                                       \
  src/capnp/serialize-async.c++                                \
  src/capnp/capability.c++                                     \
  src/capnp/membrane.c++                                       \
  src/capnp/dynamic-capability.c++                             \
  src/capnp/rpc.c++                                            \
  src/capnp/rpc.capnp.c++                                      \
  src/capnp/rpc-twoparty.c++                                   \
  src/capnp/rpc-twoparty.capnp.c++                             \
  src/capnp/persistent.capnp.c++

libcapnp_json_la_LIBADD = libcapnp.la libkj.la $(PTHREAD_LIBS)
libcapnp_json_la_LDFLAGS = -release $(SO_VERSION) -no-undefined
libcapnp_json_la_SOURCES=                                      \
  src/capnp/compat/json.c++                                    \
  src/capnp/compat/json.capnp.c++

libcapnp_websocket_la_LIBADD = libcapnp.la libcapnp-rpc.la libkj.la libkj-async.la libkj-http.la $(PTHREAD_LIBS)
libcapnp_websocket_la_LDFLAGS = -release $(SO_VERSION) -no-undefined
libcapnp_websocket_la_SOURCES=                                 \
  src/capnp/compat/websocket-rpc.c++

libcapnpc_la_LIBADD = libcapnp.la libkj.la $(PTHREAD_LIBS)
libcapnpc_la_LDFLAGS = -release $(SO_VERSION) -no-undefined
libcapnpc_la_SOURCES=                                          \
  src/capnp/compiler/type-id.h                                 \
  src/capnp/compiler/type-id.c++                               \
  src/capnp/compiler/error-reporter.h                          \
  src/capnp/compiler/error-reporter.c++                        \
  src/capnp/compiler/lexer.capnp.h                             \
  src/capnp/compiler/lexer.capnp.c++                           \
  src/capnp/compiler/lexer.h                                   \
  src/capnp/compiler/lexer.c++                                 \
  src/capnp/compiler/grammar.capnp.h                           \
  src/capnp/compiler/grammar.capnp.c++                         \
  src/capnp/compiler/parser.h                                  \
  src/capnp/compiler/parser.c++                                \
  src/capnp/compiler/resolver.h                                \
  src/capnp/compiler/generics.h                                \
  src/capnp/compiler/generics.c++                              \
  src/capnp/compiler/node-translator.h                         \
  src/capnp/compiler/node-translator.c++                       \
  src/capnp/compiler/compiler.h                                \
  src/capnp/compiler/compiler.c++                              \
  src/capnp/schema-parser.c++                                  \
  src/capnp/serialize-text.c++

bin_PROGRAMS = capnp capnpc-capnp capnpc-c++

capnp_LDADD = libcapnpc.la libcapnp-json.la libcapnp.la libkj.la $(PTHREAD_LIBS)
capnp_SOURCES =                                                \
  src/capnp/compiler/module-loader.h                           \
  src/capnp/compiler/module-loader.c++                         \
  src/capnp/compiler/capnp.c++

capnpc_capnp_LDADD = libcapnp.la libkj.la $(PTHREAD_LIBS)
capnpc_capnp_SOURCES = src/capnp/compiler/capnpc-capnp.c++

capnpc_c___LDADD = libcapnp.la libkj.la $(PTHREAD_LIBS)
capnpc_c___SOURCES = src/capnp/compiler/capnpc-c++.c++

# Symlink capnpc -> capnp.  The capnp binary will behave like the old capnpc
# binary (i.e. like "capnp compile") when invoked via this symlink.
#
# Also attempt to run ldconfig, because otherwise users get confused.  If
# it fails (e.g. because the platform doesn't have it, or because the
# user doesn't have root privileges), don't worry about it.
#
# We need to specify the path for OpenBSD.
install-exec-hook:
	ln -sf capnp $(DESTDIR)$(bindir)/capnpc
	if [ `uname` == 'OpenBSD' ]; then \
	        (ldconfig /usr/local/lib /usr/lib /usr/X11R6/lib > /dev/null 2>&1 || true); \
	else \
		ldconfig < /dev/null > /dev/null 2>&1 || true; \
	fi
uninstall-hook:
	rm -f $(DESTDIR)$(bindir)/capnpc

else LITE_MODE

install-exec-hook:
	ldconfig < /dev/null > /dev/null 2>&1 || true

endif LITE_MODE

# Source files intentionally not included in the dist at this time:
#  src/capnp/serialize-snappy*
#  src/capnp/benchmark/...
#  src/capnp/compiler/...

# Tests ==============================================================

test_capnpc_inputs =                                           \
  src/capnp/test.capnp                                         \
  src/capnp/test-import.capnp                                  \
  src/capnp/test-import2.capnp                                 \
  src/capnp/compat/json-test.capnp

test_capnpc_outputs =                                          \
  src/capnp/test.capnp.c++                                     \
  src/capnp/test.capnp.h                                       \
  src/capnp/test-import.capnp.c++                              \
  src/capnp/test-import.capnp.h                                \
  src/capnp/test-import2.capnp.c++                             \
  src/capnp/test-import2.capnp.h                               \
  src/capnp/compat/json-test.capnp.c++                         \
  src/capnp/compat/json-test.capnp.h

if USE_EXTERNAL_CAPNP

test_capnpc_middleman: $(test_capnpc_inputs)
	@$(MKDIR_P) src
	$(CAPNP) compile --src-prefix=$(srcdir)/src -o$(CAPNPC_CXX):src -I$(srcdir)/src $$(for FILE in $(test_capnpc_inputs); do echo $(srcdir)/$$FILE; done)
	touch test_capnpc_middleman

else

test_capnpc_middleman: capnp$(EXEEXT) capnpc-c++$(EXEEXT) $(test_capnpc_inputs)
	@$(MKDIR_P) src
	./capnp$(EXEEXT) compile --src-prefix=$(srcdir)/src -o./capnpc-c++$(EXEEXT):src -I$(srcdir)/src $$(for FILE in $(test_capnpc_inputs); do echo $(srcdir)/$$FILE; done)
	touch test_capnpc_middleman

endif

$(test_capnpc_outputs): test_capnpc_middleman

BUILT_SOURCES = $(test_capnpc_outputs)

check_LIBRARIES = libcapnp-test.a
libcapnp_test_a_SOURCES =                                      \
  src/capnp/test-util.c++                                      \
  src/capnp/test-util.h
nodist_libcapnp_test_a_SOURCES = $(test_capnpc_outputs)

if LITE_MODE

check_PROGRAMS = capnp-test
compiler_tests =
capnp_test_LDADD = libcapnp-test.a libcapnp.la libkj-test.la libkj.la

else !LITE_MODE

check_PROGRAMS = capnp-test capnp-evolution-test capnp-afl-testcase
if HAS_FUZZING_ENGINE
    check_PROGRAMS += capnp-llvm-fuzzer-testcase
endif
heavy_tests =                                                  \
  src/kj/async-test.c++                                        \
  src/kj/async-xthread-test.c++                                \
  src/kj/async-coroutine-test.c++                              \
  src/kj/async-unix-test.c++                                   \
  src/kj/async-unix-xthread-test.c++                           \
  src/kj/async-win32-test.c++                                  \
  src/kj/async-win32-xthread-test.c++                          \
  src/kj/async-io-test.c++                                     \
  src/kj/async-queue-test.c++                                  \
  src/kj/parse/common-test.c++                                 \
  src/kj/parse/char-test.c++                                   \
  src/kj/std/iostream-test.c++                                 \
  src/kj/compat/url-test.c++                                   \
  src/kj/compat/http-test.c++                                  \
  $(MAYBE_KJ_GZIP_TESTS)                                       \
  $(MAYBE_KJ_TLS_TESTS)                                        \
  src/capnp/canonicalize-test.c++                              \
  src/capnp/capability-test.c++                                \
  src/capnp/membrane-test.c++                                  \
  src/capnp/schema-test.c++                                    \
  src/capnp/schema-loader-test.c++                             \
  src/capnp/schema-parser-test.c++                             \
  src/capnp/dynamic-test.c++                                   \
  src/capnp/stringify-test.c++                                 \
  src/capnp/serialize-async-test.c++                           \
  src/capnp/serialize-text-test.c++                            \
  src/capnp/rpc-test.c++                                       \
  src/capnp/rpc-twoparty-test.c++                              \
  src/capnp/compat/json-test.c++                               \
  src/capnp/compat/websocket-rpc-test.c++                      \
  src/capnp/compiler/lexer-test.c++                            \
  src/capnp/compiler/type-id-test.c++
capnp_test_LDADD =                                             \
  libcapnp-test.a                                              \
  libcapnpc.la                                                 \
  libcapnp-rpc.la                                              \
  libcapnp-websocket.la                                        \
  libcapnp-json.la                                             \
  libcapnp.la                                                  \
  libkj-http.la                                                \
  $(MAYBE_KJ_GZIP_LA)                                          \
  $(MAYBE_KJ_TLS_LA)                                           \
  libkj-async.la                                               \
  libkj-test.la                                                \
  libkj.la                                                     \
  $(ASYNC_LIBS)                                                \
  $(PTHREAD_LIBS)

endif !LITE_MODE

capnp_test_CPPFLAGS = -Wno-deprecated-declarations
capnp_test_SOURCES =                                           \
  src/kj/common-test.c++                                       \
  src/kj/memory-test.c++                                       \
  src/kj/refcount-test.c++                                     \
  src/kj/array-test.c++                                        \
  src/kj/list-test.c++                                         \
  src/kj/string-test.c++                                       \
  src/kj/string-tree-test.c++                                  \
  src/kj/table-test.c++                                        \
  src/kj/map-test.c++                                          \
  src/kj/encoding-test.c++                                     \
  src/kj/exception-test.c++                                    \
  src/kj/debug-test.c++                                        \
  src/kj/arena-test.c++                                        \
  src/kj/units-test.c++                                        \
  src/kj/tuple-test.c++                                        \
  src/kj/one-of-test.c++                                       \
  src/kj/function-test.c++                                     \
  src/kj/io-test.c++                                           \
  src/kj/mutex-test.c++                                        \
  src/kj/time-test.c++                                         \
  src/kj/filesystem-test.c++                                   \
  src/kj/filesystem-disk-test.c++                              \
  src/kj/test-test.c++                                         \
  src/kj/glob-filter-test.c++                                  \
  src/capnp/common-test.c++                                    \
  src/capnp/blob-test.c++                                      \
  src/capnp/endian-test.c++                                    \
  src/capnp/endian-fallback-test.c++                           \
  src/capnp/endian-reverse-test.c++                            \
  src/capnp/layout-test.c++                                    \
  src/capnp/any-test.c++                                       \
  src/capnp/message-test.c++                                   \
  src/capnp/encoding-test.c++                                  \
  src/capnp/orphan-test.c++                                    \
  src/capnp/serialize-test.c++                                 \
  src/capnp/serialize-packed-test.c++                          \
  src/capnp/fuzz-test.c++                                      \
  $(heavy_tests)

if !LITE_MODE
capnp_evolution_test_LDADD = libcapnpc.la libcapnp.la libkj.la
capnp_evolution_test_SOURCES = src/capnp/compiler/evolution-test.c++

capnp_afl_testcase_LDADD = libcapnp-test.a libcapnp-rpc.la libcapnp.la libkj.la libkj-async.la
capnp_afl_testcase_SOURCES = src/capnp/afl-testcase.c++

if HAS_FUZZING_ENGINE
    capnp_llvm_fuzzer_testcase_LDADD = libcapnp-test.a libcapnp-rpc.la libcapnp.la libkj.la libkj-async.la
    capnp_llvm_fuzzer_testcase_SOURCES = src/capnp/llvm-fuzzer-testcase.c++
    capnp_llvm_fuzzer_testcase_LDFLAGS = $(LIB_FUZZING_ENGINE)
endif
endif !LITE_MODE

if LITE_MODE
TESTS = capnp-test
else !LITE_MODE
TESTS = capnp-test capnp-evolution-test src/capnp/compiler/capnp-test.sh
endif !LITE_MODE
