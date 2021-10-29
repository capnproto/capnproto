function(capnproto_add_kj_test module)
  # capnproto_add_kj_test(<module> MAIN <test file> [SOURCES ...] [LIBRARIES...])
  # This helper function adds a source file that has KJ tests and creates an executable for just
  # that file that's registered as a test.
  # <module>: The name of the module (i.e. capnp/kj). This is used to make sure the target name we
  #   pick is globally unique.
  # <test file>: The path to the c++ file that's the main for the test. This is used to derive the
  #   executable name.
  # SOURCES ...: If there's any additional supporting sources that need to be built into the test
  #   code.
  # LIBRARIES ...: Which libraries (aside from kj and kj-test) need to be linked into the
  #   executable.
  #
  # The executable target name will be:
  #   <module>-<test file without extension>
  #
  # Any directory separators are replaced with -. The `add_test` target is the executable target
  # name with `-run` appended to it.
  #
  # So for example compat/some-test.c++ in kj becomes:
  #   target: kj-compat-some-test
  #   test target: kj-compat-some-test-run
  #
  # The executable will be located at:
  #   c++/src/kj/compat/some-test
  # or
  #   c++/src/kj/compat/some-test.exe (on Windows)
  # relative to the build directory.

  set(options)
  set(oneValueArgs "MAIN")
  set(multiValueArgs "SOURCES" "LIBRARIES")
  cmake_parse_arguments(PARSE_ARGV 0 CAPNP_TEST "${options}" "${oneValueArgs}" "${multiValueArgs}")

  file(TO_CMAKE_PATH "${CAPNP_TEST_MAIN}" CAPNP_TEST_MAIN)
  # Sanity check. We do use `/` everywhere so not strictly necessary.

  get_filename_component(exe_dir "${CAPNP_TEST_MAIN}" DIRECTORY)
  get_filename_component(exe_name "${CAPNP_TEST_MAIN}" NAME_WE)
  if(exe_dir)
    set(target "${module}-${exe_dir}-${exe_name}")
    set(exe_name "${exe_dir}/${exe_name}")
    file(TO_NATIVE_PATH "${exe_name}${CMAKE_EXECUTABLE_SUFFIX}" native_exe_name)
  else()
    set(target "${module}-${exe_name}")
  endif()
  add_executable(${target} ${CAPNP_TEST_MAIN} ${CAPNP_TEST_SOURCES})
  target_link_libraries(${target} ${CAPNP_TEST_LIBRARIES} kj-test kj)
  set_target_properties(${target} PROPERTIES OUTPUT_NAME "${exe_name}")
  add_dependencies(check ${target})
  add_test(NAME ${target}-run COMMAND "${native_exe_name}")
endfunction()
