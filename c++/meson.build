EXE_EXTENSION = {'emscripten': 'html',
                 'windows': 'exe'}

cpp_comp = meson.get_compiler('cpp', native: false)
build_machine_cpp_comp = meson.get_compiler('cpp', native: true)

libucontext_dep = dependency('libucontext',
                             required: false,
                             native: false)

build_machine_libucontext_dep = (not meson.is_cross_build() ? libucontext_dep :
                                   dependency('libucontext',
                                              required: false,
                                              native: true))


_capnp_fibers_found = (host_machine.system() == 'windows' or
                       host_machine.system() == 'cygwin' or
                       libucontext_dep.found())

_build_machine_capnp_fibers_found = _capnp_fibers_found
if meson.is_cross_build()
  _build_machine_capnp_fibers_found = (build_machine.system() == 'windows' or
                       build_machine.system() == 'cygwin' or
                       libucontext_dep.found())
endif

_with_libucontext = libucontext_dep.found()
with_fibers = get_option('with_fibers').disable_if(
                          not _capnp_fibers_found and
                          not cpp_comp.has_function('makecontext') and
                          not libucontext_dep.found(),
                          error_message: 'Missing makecontext, getcontext, setcontext or swapcontext symbol in libc and no libucontext found: KJ will not be able to build with fibers. set -Dwith_fibers=disabled')


threads_dep = declare_dependency()
build_machine_threads_dep = dependency('threads', native: true)
if cpp_comp.get_id() == 'msvc'
  add_project_arguments('/wo4503')
else
  add_project_arguments(['-Wno-strict-aliasing',
                         '-Wno-sign-compare',
                         '-Wno-unused-parameter'],
                        language: 'cpp',
                        native: false)
  if not (host_machine.system() == 'android')
    threads_dep = dependency('threads')
  endif
  if host_machine.system() == 'emscripten'
      threads_dep = declare_dependency(compile_args: '-pthread', link_args: '-pthread')
  endif
endif


subdir('src')
