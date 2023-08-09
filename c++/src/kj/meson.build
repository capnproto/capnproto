kj_sources_lite = [
  'array.c++',
  'list.c++',
  'common.c++',
  'debug.c++',
  'exception.c++',
  'io.c++',
  'memory.c++',
  'mutex.c++',
  'string.c++',
  'source-location.c++',
  'hash.c++',
  'table.c++',
  'thread.c++',
  'main.c++',
  'arena.c++',
  'test-helpers.c++',
  'units.c++',
  'encoding.c++']

kj_sources_heavy = [
  'refcount.c++',
  'string-tree.c++',
  'time.c++',
  'filesystem.c++',
  'filesystem-disk-unix.c++',
  'filesystem-disk-win32.c++',
  'parse/char.c++']

kj_sources = kj_sources_heavy + kj_sources_lite

kj_headers = [
  'common.h',
  'units.h',
  'memory.h',
  'refcount.h',
  'array.h',
  'list.h',
  'vector.h',
  'string.h',
  'string-tree.h',
  'source-location.h',
  'hash.h',
  'table.h',
  'map.h',
  'encoding.h',
  'exception.h',
  'debug.h',
  'arena.h',
  'io.h',
  'tuple.h',
  'one-of.h',
  'function.h',
  'mutex.h',
  'thread.h',
  'threadlocal.h',
  'filesystem.h',
  'time.h',
  'main.h',
  'win32-api-version.h',
  'windows-sanity.h']



kj_parse_headers = ['parse/common.h',
                    'parse/char.h']

kj_std_headers = ['std/iostream.h']

install_lib = not get_option('_meson_nested_invocation')

kj_lib = library('kj', kj_sources,
                 dependencies: [threads_dep],
                 implicit_include_directories: false,
                 install: install_lib)

build_machine_kj_lib = kj_lib
if meson.is_cross_build()
  build_machine_kj_lib = library('bm_kj', kj_sources,
          dependencies: [threads_dep],
          implicit_include_directories: false,
          native: true)
endif


kj_dep = declare_dependency(link_with: [kj_lib],
                            dependencies: [threads_dep],
                            include_directories: '..')

build_machine_kj_dep = kj_dep
if meson.is_cross_build()
  build_machine_kj_dep = declare_dependency(
    link_with: [build_machine_kj_lib],
    dependencies: [build_machine_threads_dep],
    include_directories: '..')
endif


kj_async_sources = ['async.c++',
  'async-unix.c++',
  'async-win32.c++',
  'async-io-win32.c++',
  'async-io.c++',
  'async-io-unix.c++',
  'timer.c++']

kj_async_headers = [
  'async-prelude.h',
  'async.h',
  'async-inl.h',
  'async-unix.h',
  'async-win32.h',
  'async-io.h',
  'async-queue.h',
  'timer.h']


async_compile_args = []
if with_fibers.allowed()
  async_compile_args += '-DKJ_USE_FIBERS'
else
  async_compile_args += '-DKJ_USE_FIBERS=0'
endif


kj_async_lib = library('kj-async', kj_async_sources,
                       cpp_args: async_compile_args,
                       dependencies: [kj_dep,
                                      libucontext_dep],
                       implicit_include_directories: false,
                       install: install_lib)

build_machine_kj_async_lib = kj_async_lib
if meson.is_cross_build()
  build_machine_kj_async_lib = library('bm_kj-async', kj_async_sources,
          cpp_args: async_compile_args,
          dependencies: [build_machine_kj_dep,
                         build_machine_libucontext_dep],
          implicit_include_directories: false,
          native: true)
endif


ws2_32_dep = dependency('ws2_32', required: false)
kj_async_dep = declare_dependency(link_with: kj_async_lib,
                                  include_directories: '..',
                                  dependencies: [libucontext_dep,
                                                 threads_dep,
                                                 ws2_32_dep])

build_machine_kj_async_dep = kj_async_dep
if meson.is_cross_build()
  build_machine_kj_async_dep = declare_dependency(link_with: build_machine_kj_async_lib,
                                  include_directories: '..',
                                  dependencies: [build_machine_libucontext_dep,
                                                 build_machine_threads_dep,
                                                 ws2_32_dep])
endif

kj_http_sources = ['compat/url.c++',
  'compat/http.c++']

kj_http_headers = [
  'compat/url.h',
  'compat/http.h']

kj_http_lib = library('kj-http', kj_http_sources,
                      dependencies: [kj_async_dep, kj_dep],
                      implicit_include_directories: false,
                      install: install_lib)
if meson.is_cross_build()
  build_machine_kj_http_lib = library(
    'bm_kj-http', kj_http_sources,
    dependencies: [build_machine_kj_async_dep, build_machine_kj_dep],
    implicit_include_directories: false,
    native: meson.is_cross_build())
endif

kj_http_dep = declare_dependency(link_with: kj_http_lib,
                                include_directories: '..',
                                dependencies: [kj_async_dep, kj_dep])

build_machine_kj_http_dep = kj_http_dep
if meson.is_cross_build()
  build_machine_kj_http_dep = declare_dependency(
    link_with: build_machine_kj_http_lib,
    include_directories: '..',
    dependencies: [build_machine_kj_async_dep,
                   build_machine_kj_dep])
endif


kj_tls_sources = ['compat/readiness-io.c++',
                  'compat/tls.c++']

kj_tls_headers = ['compat/readiness-io.h',
                  'compat/tls.h']


openssl_dep = dependency('openssl',
                         required: get_option('with_openssl'),
                         disabler: true)


tls_compile_args = openssl_dep.found() ? ['-DKJ_HAS_OPENSSL'] : []
kj_tls_lib = library('kj-tls', kj_tls_sources,
                     cpp_args: tls_compile_args,
                     dependencies: [kj_async_dep, kj_dep, openssl_dep],
                     implicit_include_directories: false,
                     install: install_lib)


kj_tls_dep = declare_dependency(link_with: kj_tls_lib,
                                include_directories: '..',
                                dependencies: [kj_async_dep])

kj_gzip_sources = 'compat/gzip.c++'
kj_gzip_headers = 'compat/gzip.h'

zlib_dep = dependency('zlib',
                      required: get_option('with_zlib'),
                      native: false,
                      disabler: true)

gzip_compile_args = zlib_dep.found() ? ['-DKJ_HAS_ZLIB=1'] : []
kj_gzip_lib = library('kj-gzip', kj_gzip_sources,
                      cpp_args: gzip_compile_args,
                      dependencies: [kj_dep, kj_async_dep, zlib_dep],
                      implicit_include_directories: false,
                      install: install_lib)


kj_gzip_dep = declare_dependency(link_with: kj_gzip_lib,
                                 include_directories: '..',
                                 dependencies: [kj_async_dep,
                                                kj_dep,
                                                zlib_dep])
kj_test_sources = 'test.c++'
kj_test_headers = 'test.h'


kj_test_compat_headers = 'compat/gtest.h'

kj_test_lib = library('kj-test', kj_test_sources,
                      dependencies: kj_dep,
                      include_directories: '..',
                      implicit_include_directories: false,
                      install: install_lib)

kj_test_dep = declare_dependency(link_with: kj_test_lib,
                                 dependencies: kj_dep)

if install_lib
  install_headers(kj_headers +
                kj_std_headers +
                kj_parse_headers +
                kj_async_headers +
                kj_http_headers +
                kj_tls_headers +
                kj_gzip_headers +
                kj_test_headers,
                subdir: 'kj',
                preserve_path: true)
endif

if get_option('build_testing')
  kj_tests = executable('kj-tests',
           ['common-test.c++',
            'memory-test.c++',
            'array-test.c++',
            'list-test.c++',
            'string-test.c++',
            'table-test.c++',
            'map-test.c++',
            'exception-test.c++',
            'debug-test.c++',
            'io-test.c++',
            'mutex-test.c++',
            'time-test.c++',
            'threadlocal-test.c++',
            'test-test.c++',
            'std/iostream-test.c++'],
           dependencies: [kj_test_dep, kj_dep],
           implicit_include_directories: false)

  test('kj-tests-run', kj_tests)

  extra_tests = []
  if openssl_dep.found()
    extra_tests += 'compat/tls-test.c++'
  endif
  if host_machine.system() != 'emscripten' and host_machine.system() != 'windows'
    extra_tests += 'async-unix-test.c++'
  endif
  kj_heavy_tests = executable('kj-heavy-tests',
      ['async-test.c++',
      'async-xthread-test.c++',
      'async-coroutine-test.c++',
      'async-unix-xthread-test.c++',
      'async-win32-test.c++',
      'async-win32-xthread-test.c++',
      'async-io-test.c++',
      'async-queue-test.c++',
      'refcount-test.c++',
      'string-tree-test.c++',
      'encoding-test.c++',
      'arena-test.c++',
      'units-test.c++',
      'tuple-test.c++',
      'one-of-test.c++',
      'function-test.c++',
      'filesystem-test.c++',
      'filesystem-disk-test.c++',
      'parse/common-test.c++',
      'parse/char-test.c++',
      'compat/url-test.c++',
      'compat/http-test.c++',
      'compat/gzip-test.c++',
      extra_tests],
      dependencies: [kj_http_dep,
                     kj_gzip_dep,
                     kj_tls_dep,
                     kj_async_dep,
                     kj_test_dep,
                     openssl_dep,
                     kj_dep],
      cpp_args: openssl_dep.found()
                ? '-DKJ_HAS_OPENSSL' : '',
      implicit_include_directories: false)

      test('kj-heavy-tests-run', kj_heavy_tests)
endif
