# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# The plot without a window, a file or a toolkit.
#
# Everything the viewer decides about a line before a pixel is touched -- the
# envelope it is folded into, the pyramid every zoom is folded out of, the
# policy over which runs to hold, where each point lands and how a stroke is
# built out of triangles -- is arithmetic over doubles, and it is all here. The
# viewer is one of its users: it reaches this with add_subdirectory() and links
# plotcore::core like anything else would.
#
# A project of its own, buildable with nothing but CMake and a C++20 compiler:
# no vcpkg, no Qt, no HDF5. That is what lets the C++, Python and Rust
# libraries built on it stand alone, and what CI holds it to.
#
# One directory per language, each with its library, its tests and its demo.
# This project builds two of them: cpp/, the core and the Qt widget over it,
# and c/, the C interface the other two are built on. python/ and rust/ are
# built by their own tools, from c/ and from cpp/'s sources. What is decided
# here is what all of them share: which parts are built, how, and what is
# installed.

cmake_minimum_required(VERSION 3.26)
project(plotcore VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)

# On when this is the top level, and whatever the including project says when
# it is not -- a normal variable set before add_subdirectory() is honoured,
# which is how the viewer hands over its own BUILD_TESTING.
option(PLOTCORE_BUILD_TESTS "Build plotcore's tests" ${PROJECT_IS_TOP_LEVEL})
# Off inside another project: the viewer installs one executable and nothing
# that looks like a development package.
option(PLOTCORE_INSTALL "Install plotcore's headers, library and CMake package"
       ${PROJECT_IS_TOP_LEVEL})
# The C interface, which the Python and Rust libraries are built on. Off inside
# the viewer, which links the C++ directly.
option(PLOTCORE_BUILD_CAPI "Build plotcore_c, the C interface" ${PROJECT_IS_TOP_LEVEL})
# The one part of this project that needs a toolkit, so off unless asked for.
option(PLOTCORE_WITH_QT "Build plotcore::qt and its demo" OFF)

# A wheel's library is loaded by whatever Python is on the machine, and Python
# ships the C runtime but not the C++ one (MSVCP140.dll): so inside a wheel the
# MSVC runtime is linked in, as -static-libstdc++ links libstdc++ in in c/. Not
# otherwise, because a static archive's runtime is its consumers' to match.
if(SKBUILD AND MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(MSVC)
    add_compile_options(/W4 /utf-8)
  else()
    add_compile_options(-Wall -Wextra)
  endif()
endif()

find_package(Threads REQUIRED)

# Before anything that adds a test: enable_testing() only reaches the
# directories added after it. The Qt demo's self-test is added under
# cpp/qt-demo, and was registered nowhere while this came after it -- H5Scope's
# include(CTest), which runs before it adds this project, was hiding that.
if(PLOTCORE_BUILD_TESTS)
  enable_testing()
endif()

# plotcore::core, plotcore::qt and the Qt demo, and their tests.
add_subdirectory(cpp)

# plotcore::c and its test, which is also what writes golden/expected.txt.
if(PLOTCORE_BUILD_CAPI)
  add_subdirectory(c)
endif()

# --- a Python wheel ---------------------------------------------------------
# scikit-build-core, from python/pyproject.toml: the C interface and
# nothing else, beside the package that loads it with ctypes. It links no
# Python, which is what lets one wheel per platform serve every version.
if(SKBUILD)
  if(NOT PLOTCORE_BUILD_CAPI)
    message(FATAL_ERROR "a wheel is the C interface: PLOTCORE_BUILD_CAPI must be ON")
  endif()
  install(TARGETS plotcore_c
          LIBRARY DESTINATION plotcore
          RUNTIME DESTINATION plotcore)
  # A wheel conveys the library, so it carries the licence (GPL-3.0 section 4),
  # where PEP 639 puts one. scikit-build-core finds licences beside
  # pyproject.toml, and python/ has no copy of this one.
  install(FILES LICENSE DESTINATION "${SKBUILD_METADATA_DIR}/licenses")
endif()

if(PLOTCORE_INSTALL)
  include(CMakePackageConfigHelpers)
  install(TARGETS plotcore_core EXPORT plotcoreTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  if(PLOTCORE_BUILD_CAPI)
    install(TARGETS plotcore_c EXPORT plotcoreTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  endif()
  install(DIRECTORY cpp/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT plotcoreTargets NAMESPACE plotcore::
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plotcore)
  configure_package_config_file(cmake/plotcoreConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/plotcoreConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plotcore)
  write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/plotcoreConfigVersion.cmake"
    COMPATIBILITY SameMinorVersion)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plotcoreConfig.cmake"
                "${CMAKE_CURRENT_BINARY_DIR}/plotcoreConfigVersion.cmake"
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plotcore)
endif()
