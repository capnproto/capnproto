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

inc = '../../../../capnproto-c++-0.7.0/src'	 #BUG: should be '../../src/' but it's not linking properly ATM

executable('client'
, sources: ['client.c++', 'interface.capnp.c++']
, include_directories: [inc]
, cpp_pch: '.pch/pch.h'
, link_args: ['-lcapnp', '-lcapnp-rpc', '-lkj', '-lkj-async'])

executable('proxy'
, sources: ['proxy.c++', 'secure.c++', 'interface.capnp.c++']
, include_directories: [inc]
, cpp_pch: '.pch/pch.h'
, link_args: ['-lcapnp', '-lcapnp-rpc', '-lkj', '-lkj-async'])

executable('service'
, sources: ['service.c++', 'secure.c++', 'interface.capnp.c++']
, include_directories: [inc]
, cpp_pch: '.pch/pch.h'
, link_args: ['-lcapnp', '-lcapnp-rpc', '-lkj', '-lkj-async'])


#BUG: link_args only works on linux due to -lpthread
#BUG: global arguments are specific to gcc
#BUG: build script should generate capnp sources, it's tricky with meson because meson checks for source files at configure time