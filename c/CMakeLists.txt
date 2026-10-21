# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: BSD-3-Clause

# plotcore in C: what the Python and Rust libraries are built on.
# plotcore::Figure behind a C header, as one shared library that exports that
# header and nothing else.

add_library(plotcore_c SHARED plotcore.cpp)
add_library(plotcore::c ALIAS plotcore_c)
set_target_properties(plotcore_c PROPERTIES
  EXPORT_NAME c
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  # Absolute, because the root installs it, and a relative one would be read
  # from there.
  PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/plotcore.h
)
if(NOT SKBUILD)
  # Versioned where it is installed as a library. Not inside a wheel, which
  # is a zip: the two symlinks would go into it as two more copies.
  set_target_properties(plotcore_c PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
endif()
target_compile_definitions(plotcore_c PRIVATE
  PLOTCORE_VERSION_STRING="${PROJECT_VERSION}")
target_include_directories(plotcore_c PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(plotcore_c PRIVATE plotcore::core)
if(NOT MSVC AND NOT APPLE)
  # One file to hand to Python or Rust: the C++ runtime inside it rather
  # than beside it, for the release floor's reason (CLAUDE.md, "The floor").
  target_link_options(plotcore_c PRIVATE -static-libstdc++ -static-libgcc
                      LINKER:--exclude-libs,ALL)
endif()

# --- the C interface, from C ----------------------------------------------
# Compiled as C, because a header only a C++ compiler accepts is no C
# interface; and run over the golden script, whose digest the Python and Rust
# bindings are held to (golden/README.md).
if(PLOTCORE_BUILD_TESTS)
  enable_language(C)
  add_executable(test_capi tests/test_capi.c)
  set_target_properties(test_capi PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_link_libraries(test_capi PRIVATE plotcore::c)
  if(NOT WIN32)
    target_link_libraries(test_capi PRIVATE m)
  endif()
  add_test(NAME capi
           COMMAND test_capi ${PROJECT_SOURCE_DIR}/golden/script.txt
                             ${PROJECT_SOURCE_DIR}/golden/expected.txt)
  # The library beside the test on Windows, where nothing else finds a DLL.
  set_tests_properties(capi PROPERTIES ENVIRONMENT_MODIFICATION
                       "PATH=path_list_prepend:$<TARGET_FILE_DIR:plotcore_c>")
endif()
