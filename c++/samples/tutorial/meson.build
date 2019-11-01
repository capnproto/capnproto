#meson build file, see https://mesonbuild.com/Syntax.html

# usage:
# precondition: build capnp
# precondition: install meson and ninja
# edit init so that lib and inc links point to correct directories. lib/capnp and inc/capnp search paths must work.
# 
# ./init
# cd out
# ninja
#
# look in out/meson-out for client and service executables
# run ./clean if you want to clean up

project('capnp_sample', 'cpp', default_options : [
	'cpp_std=c++17',
	'layout=flat',
	'strip=false',
	'warning_level=3'])

add_global_arguments('-Wno-non-virtual-dtor', '-Wno-unused-parameter', '-Wno-unused-variable', language: 'cpp')

clientSources = files('client.c++')
serviceSources = files('service.c++')

executable('client'
, sources: ['client.c++', 'interface.capnp.c++']
, include_directories: ['.', '../../src/']
, cpp_pch: '.pch/pch.h'
, link_args: ['-lcapnp', '-lcapnp-rpc', '-lkj', '-lkj-async'])
#, link_args: ['-L ../../src/capnp', '-lcapnp', '-lcapnp-rpc', '-lcapnpc', '-lcapnp-json', '-lkj', '-lkj-async', '-lpthread'])

#executable('service'
#, sources: ['service.c++', 'interface.capnp.c++']
#, include_directories: ['../../src/']
#, cpp_pch: '.pch/pch.h'
#, link_args: ['-lcapnp', '-lcapnp-rpc', '-lkj', '-lkj-async'])
#, link_args: ['-lcapnp', '-lcapnp-rpc', '-lcapnpc', '-lcapnp-json', '-lkj', '-lkj-async', '-lkj-http', '-lkj-tls', '-lpthread'])


#BUG: link_args only works on linux due to -lpthread
#BUG: global arguments are specific to gcc
#BUG: build script should generate capnp sources, it's tricky with meson because meson checks for source files at configure time