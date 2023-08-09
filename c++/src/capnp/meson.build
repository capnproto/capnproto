# capnp ========================================================================

capnp_sources_lite = [
  'c++.capnp.c++',
  'blob.c++',
  'arena.c++',
  'layout.c++',
  'list.c++',
  'any.c++',
  'message.c++',
  'schema.capnp.c++',
  'stream.capnp.c++',
  'serialize.c++',
  'serialize-packed.c++']

capnp_sources_heavy = [
  'schema.c++',
  'schema-loader.c++',
  'dynamic.c++',
  'stringify.c++']

capnp_sources = capnp_sources_lite + capnp_sources_heavy

capnp_headers = [
  'c++.capnp.h',
  'common.h',
  'blob.h',
  'endian.h',
  'layout.h',
  'orphan.h',
  'list.h',
  'any.h',
  'message.h',
  'capability.h',
  'membrane.h',
  'dynamic.h',
  'schema.h',
  'schema.capnp.h',
  'stream.capnp.h',
  'schema-lite.h',
  'schema-loader.h',
  'schema-parser.h',
  'pretty-print.h',
  'serialize.h',
  'serialize-async.h',
  'serialize-packed.h',
  'serialize-text.h',
  'pointer-helpers.h',
  'generated-header-support.h',
  'raw-schema.h']

capnp_compat_headers = 'compat/std-iterator.h'

capnp_schemas = [
  'c++.capnp',
  'schema.capnp',
  'stream.capnp']

install_lib = not get_option('_meson_nested_invocation')

capnp_lib = library('capnp',
                    capnp_sources,
                    dependencies: [kj_dep],
                    include_directories: '..',
                    implicit_include_directories: false,
                    install: install_lib)

build_machine_capnp_lib = capnp_lib
if meson.is_cross_build()
  build_machine_capnp_lib = library('bm_capnp',
                    capnp_sources,
                    dependencies: [build_machine_kj_dep],
                    include_directories: '..',
                    implicit_include_directories: false,
                    native: true)
endif


capnp_dep = declare_dependency(link_with: capnp_lib,
                               dependencies: kj_dep,
                               include_directories: '..')

build_machine_capnp_dep = capnp_dep
if meson.is_cross_build()
  build_machine_capnp_dep = declare_dependency(link_with: build_machine_capnp_lib,
                                               dependencies: build_machine_kj_dep,
                                               include_directories: '..')
endif


capnp_rpc_sources = [
  'serialize-async.c++',
  'capability.c++',
  'membrane.c++',
  'dynamic-capability.c++',
  'rpc.c++',
  'rpc.capnp.c++',
  'rpc-twoparty.c++',
  'rpc-twoparty.capnp.c++',
  'persistent.capnp.c++',
  'ez-rpc.c++']

capnp_rpc_headers = [
  'rpc-prelude.h',
  'rpc.h',
  'rpc-twoparty.h',
  'rpc.capnp.h',
  'rpc-twoparty.capnp.h',
  'persistent.capnp.h',
  'ez-rpc.h']

capnp_rpc_schemas = [
  'rpc.capnp',
  'rpc-twoparty.capnp',
  'persistent.capnp']

capnp_rpc_lib = library('capnp-rpc', capnp_rpc_sources, include_directories: '..',
                        dependencies: [capnp_dep, kj_async_dep, kj_dep],
                        implicit_include_directories: false,
                        install: install_lib)
build_machine_capnp_rpc_lib = capnp_rpc_lib
if meson.is_cross_build()
  build_machine_capnp_rpc_lib = library('bm_capnp-rpc',
                                        capnp_rpc_sources,
                                        include_directories: '..',
                        dependencies: [build_machine_capnp_dep, build_machine_kj_async_dep, build_machine_kj_dep],
                        implicit_include_directories: false,
                        native: true)
endif

capnp_rpc_dep = declare_dependency(link_with: capnp_rpc_lib,
                                   dependencies:[capnp_dep, kj_async_dep, kj_dep],
                                   include_directories: '..')
build_machine_capnp_rpc_dep = capnp_rpc_dep
if meson.is_cross_build()
  build_machine_capnp_rpc_dep = declare_dependency(link_with: build_machine_capnp_rpc_lib,
                                   dependencies:[build_machine_capnp_dep, build_machine_kj_async_dep,
                                                 build_machine_kj_dep],
                                   include_directories: '..')
endif


# capnp-json ========================================================================

capnp_json_sources = [
  'compat/json.c++',
  'compat/json.capnp.c++'
]

capnp_json_headers = [
  'compat/json.h',
  'compat/json.capnp.h'
]

capnp_json_schemas = 'compat/json.capnp'

capnp_json_lib = library('capnp-json', capnp_json_sources,
                         dependencies:[capnp_dep, kj_async_dep, kj_dep],
                         implicit_include_directories: false,
                         install: install_lib)

build_machine_capnp_json_lib = capnp_json_lib
if meson.is_cross_build()
  build_machine_capnp_json_lib = library(
    'bm_capnp-json', capnp_json_sources,
    dependencies:[build_machine_capnp_dep, build_machine_kj_async_dep,
                  build_machine_kj_dep],
    implicit_include_directories: false,
    native: true)
endif


capnp_json_dep = declare_dependency(link_with: capnp_json_lib,
                                    include_directories: '..',
                                    dependencies:[capnp_dep, kj_async_dep, kj_dep])

build_machine_capnp_json_dep = capnp_json_dep
if meson.is_cross_build()
  build_machine_capnp_json_dep = declare_dependency(link_with: build_machine_capnp_json_lib,
                                                    include_directories: '..',
                                    dependencies:[build_machine_capnp_dep, build_machine_kj_async_dep, build_machine_kj_dep])
endif


capnp_websocket_sources = 'compat/websocket-rpc.c++'
capnp_websocket_headers = 'compat/websocket-rpc.h'


capnp_websocket_lib = library('capnp-websocket', capnp_websocket_sources,
                              dependencies:[capnp_dep, capnp_rpc_dep, kj_http_dep,
                                            kj_async_dep, kj_dep],
                              implicit_include_directories: false,
                              install: install_lib)

build_machine_capnp_websocket_lib = capnp_websocket_lib
if meson.is_cross_build()
  build_machine_capnp_websocket_lib = library('bm_capnp-websocket', capnp_websocket_sources,
                              dependencies:[build_machine_capnp_dep, build_machine_capnp_rpc_dep,
                                            build_machine_kj_http_dep,
                                            build_machine_kj_async_dep, build_machine_kj_dep],
                              implicit_include_directories: false,
                              native: true)
endif

capnp_websocket_dep = declare_dependency(link_with: capnp_websocket_lib,
                                         include_directories: '..',
                                         dependencies: [capnp_dep,
                                                        capnp_rpc_dep,
                                                        kj_http_dep,
                                                        kj_async_dep, kj_dep])

build_machine_capnp_websocket_dep = capnp_websocket_dep
if meson.is_cross_build()
  build_machine_capnp_websocket_dep = declare_dependency(link_with: build_machine_capnp_websocket_lib,
                                         include_directories: '..',
                                         dependencies: [build_machine_capnp_dep,
                                                        build_machine_capnp_rpc_dep,
                                                        build_machine_kj_http_dep,
                                                        build_machine_kj_async_dep,
                                                        build_machine_kj_dep])
endif

if install_lib
  install_headers(capnp_headers +
                capnp_schemas +
                capnp_rpc_schemas +
                capnp_rpc_headers +
                capnp_json_headers +
                capnp_json_schemas +
                capnp_websocket_headers,
                subdir: 'capnp',
                preserve_path: true)
endif

# Tools/Compilers: target is build machine ==============================================================
capnpc_sources = [
  'compiler/type-id.c++',
  'compiler/error-reporter.c++',
  'compiler/lexer.capnp.c++',
  'compiler/lexer.c++',
  'compiler/grammar.capnp.c++',
  'compiler/parser.c++',
  'compiler/generics.c++',
  'compiler/node-translator.c++',
  'compiler/compiler.c++',
  'schema-parser.c++',
  'serialize-text.c++']


capnpc_lib = library('capnpc', capnpc_sources,
                     dependencies: [capnp_dep, kj_dep],
                     include_directories: '..',
                     implicit_include_directories: false,
                     native: false,
                     install: install_lib)

# install_symlink('capnpc',
#                 install_dir: get_option('bindir'),
#                 pointing_to: 'capnp')
#
build_machine_capnpc_lib = capnpc_lib
if meson.is_cross_build()
  build_machine_capnpc_lib =library(
    'bm_capnpc', capnpc_sources,
    dependencies: [build_machine_capnp_dep, build_machine_kj_dep],
    include_directories: '..',
    implicit_include_directories: false,
    native: true)
endif

capnpc_dep = declare_dependency(link_with: capnpc_lib,
                                include_directories: '..',
                                dependencies: [capnp_dep,
                                               kj_dep])

build_machine_capnpc_dep = capnpc_dep
if meson.is_cross_build()
  build_machine_capnpc_dep = declare_dependency(
    link_with: build_machine_capnpc_lib,
    include_directories: '..',
    dependencies: [build_machine_capnp_dep,
                   build_machine_kj_dep])
endif


capnp_include_dir = get_option('prefix') / get_option('includedir')
project_version = meson.project_version()
capnpc = executable('capnpc', sources: ['compiler/module-loader.c++',
                                        'compiler/capnp.c++'],
                    dependencies: [build_machine_capnpc_dep,
                                   build_machine_capnp_json_dep,
                                   build_machine_capnp_dep,
                                   build_machine_kj_dep],
                    cpp_args: [f'-DCAPNP_INCLUDE_DIR="@capnp_include_dir@"',
                               f'-DVERSION="@project_version@"'],
                    native: true,
                    install: not meson.is_cross_build())

capnpc_cpp = executable('capnpc-c++', sources: [ 'compiler/capnpc-c++.c++'],
                        include_directories: '..',
                        implicit_include_directories: false,
                        dependencies: [build_machine_capnp_dep, build_machine_kj_dep],
                        native: true,
                        install: not meson.is_cross_build())

capnpc_capnp = executable('capnpc-capnp', sources: [ 'compiler/capnpc-capnp.c++'],
                          include_directories: '..',
                          implicit_include_directories: false,
                          dependencies: [build_machine_capnp_dep, build_machine_kj_dep],
                          native: true,
                          install: not meson.is_cross_build())


if get_option('build_testing')
  test_capnp_files = [
    'test.capnp',
    'test-import.capnp',
    'test-import2.capnp',
    'compat/json-test.capnp'
  ]


  capnp_cpp_plugin_path = capnpc_cpp.full_path()
  test_capnp_cpp_h_files = []
  foreach test_capnp_file : test_capnp_files
    test_capnp_file_parts = test_capnp_file.split('/')
    src_prefix = '--src-prefix=@CURRENT_SOURCE_DIR@' + (test_capnp_file_parts.length() > 1 ? '/' + test_capnp_file_parts[0] : '')
    test_capnp_cpp_h_files += custom_target(
      depends: capnpc_cpp,
      command: [capnpc, src_prefix,
                '-I@CURRENT_SOURCE_DIR@/..',
                f'-o@capnp_cpp_plugin_path@:' +  '@OUTDIR@',
                '@INPUT@'],
      input: test_capnp_file,
      output: ['@BASENAME@.capnp.c++', '@BASENAME@.capnp.h'])
  endforeach

  test_dependencies =  [capnp_dep,
                        kj_test_dep,
                        kj_dep,
                        capnp_json_dep,
                        capnp_rpc_dep,
                        capnp_websocket_dep,
                        capnp_dep,
                        capnpc_dep,
                        kj_http_dep,
                        kj_async_dep,
                        kj_test_dep, kj_dep]

  capnp_tests = executable(
    'capnp-tests',
    sources: ['common-test.c++',
     'blob-test.c++',
     'endian-test.c++',
     'endian-fallback-test.c++',
     'layout-test.c++',
     'any-test.c++',
     'message-test.c++',
     'encoding-test.c++',
     'orphan-test.c++',
     'serialize-test.c++',
     'serialize-packed-test.c++',
     'canonicalize-test.c++',
     'fuzz-test.c++',
     'test-util.c++',
     test_capnp_cpp_h_files],
    dependencies: test_dependencies)

  test('capnp-tests-run', capnp_tests)


  capnp_heavy_tests = executable('capnp-heavy-tests',
    sources: [
      'endian-reverse-test.c++',
     'capability-test.c++',
     'membrane-test.c++',
     'schema-test.c++',
     'schema-loader-test.c++',
     'schema-parser-test.c++',
     'dynamic-test.c++',
     'stringify-test.c++',
     'serialize-async-test.c++',
     'serialize-text-test.c++',
     'rpc-test.c++',
     'rpc-twoparty-test.c++',
     'ez-rpc-test.c++',
     'compiler/lexer-test.c++',
     'compiler/type-id-test.c++',
     'test-util.c++',
     'compat/json-test.c++',
     'compat/websocket-rpc-test.c++',
     test_capnp_cpp_h_files],
    include_directories: [],
    dependencies: test_dependencies,
    cpp_args: meson.get_compiler('cpp').get_supported_arguments('-Wno-deprecated-declarations'))

   test('capnp-heavy-tests-run',
        capnp_heavy_tests)
  capnp_evolution_tests = executable('capnp-evolution-tests',
                                     sources: 'compiler/evolution-test.c++',
                                     dependencies: [capnpc_dep, capnp_dep, kj_dep])

  test('capnp-evolution-tests-run',
       capnp_evolution_tests)
endif

if meson.is_cross_build()
  meson_prog = find_program('meson')
  meson.add_install_script(meson_prog, 'setup','-D_meson_nested_invocation=true',
                           '-Dprefix=@0@'.format(get_option('prefix')),
                           '-Dbuild_testing=false', meson.project_source_root(), 'build-compilers')
  meson.add_install_script(meson_prog, 'compile', '-C', '@0@/build-compilers'.format(meson.project_build_root()))
  meson.add_install_script(meson_prog, 'install', '-C', '@0@/build-compilers'.format(meson.project_build_root()))
endif
