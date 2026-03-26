cmake_minimum_required(VERSION 3.20.0)

project(caffeine LANGUAGES C CXX Fortran)
include(ExternalProject)

option(USE_FPM "Specifies whether you want to use FPM to build external dependencies." OFF)
if(NOT DEFINED BUILD_SHARED_LIBS)
  set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries" FORCE)
endif()

message(STATUS "CMAKE_Fortran_COMPILER_ID: ${CMAKE_Fortran_COMPILER_ID}")
if(CMAKE_Fortran_COMPILER_ID STREQUAL "LLVMFlang")
  # Make ISO_Fortran_binding.h visible to the C compiler
  get_filename_component(_FLANG_REAL_PATH "${CMAKE_Fortran_COMPILER}" REALPATH)
  get_filename_component(_FLANG_BIN_DIR "${_FLANG_REAL_PATH}" DIRECTORY)
  get_filename_component(_FLANG_PREFIX "${_FLANG_BIN_DIR}" DIRECTORY)
  set(_FLANG_INCLUDE_DIR "${_FLANG_PREFIX}/include/flang")
  if(EXISTS "${_FLANG_INCLUDE_DIR}/ISO_Fortran_binding.h")
    message(STATUS "Found ISO_Fortran_binding.h in ${_FLANG_INCLUDE_DIR}")
    # Only add for C/CXX — flang's .mod files in this directory are incompatible
    # with other Fortran compilers and must not appear on the Fortran include path.
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-isystem ${_FLANG_INCLUDE_DIR}>")
  endif()
elseif(CMAKE_Fortran_COMPILER_ID STREQUAL "LFortran")
  set(CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} --cpp --realloc-lhs-arrays --separate-compilation --no-style-suggestions --implicit-argument-casting")
  # LFortran doesn't yet provide ISO_Fortran_binding.h; use flang's copy as a temporary fallback
  find_program(_FLANG_COMPILER flang)
  if(_FLANG_COMPILER)
    get_filename_component(_FLANG_REAL_PATH "${_FLANG_COMPILER}" REALPATH)
    get_filename_component(_FLANG_BIN_DIR "${_FLANG_REAL_PATH}" DIRECTORY)
    get_filename_component(_FLANG_PREFIX "${_FLANG_BIN_DIR}" DIRECTORY)
    set(_FLANG_INCLUDE_DIR "${_FLANG_PREFIX}/include/flang")
    if(EXISTS "${_FLANG_INCLUDE_DIR}/ISO_Fortran_binding.h")
      message(STATUS "LFortran: using ISO_Fortran_binding.h from flang in ${_FLANG_INCLUDE_DIR}")
      # Only add for C/CXX — flang's .mod files in this directory would crash
      # LFortran which expects its own binary module format.
      add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-isystem ${_FLANG_INCLUDE_DIR}>")
    endif()
  endif()
elseif(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -fcoarray=single")
  # Make ISO_Fortran_binding.h visible to the C compiler without pulling in
  # GCC's full include directory (which contains stddef.h incompatible with Clang).
  execute_process(
    COMMAND ${CMAKE_Fortran_COMPILER} -print-file-name=include
    OUTPUT_VARIABLE _GFORTRAN_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(_GFORTRAN_BINDING_H "${_GFORTRAN_INCLUDE_DIR}/ISO_Fortran_binding.h")
  if(EXISTS "${_GFORTRAN_BINDING_H}")
    message(STATUS "Found ISO_Fortran_binding.h in ${_GFORTRAN_INCLUDE_DIR}")
    set(_CFI_INCLUDE_DIR "${CMAKE_BINARY_DIR}/gfortran-cfi-include")
    file(MAKE_DIRECTORY "${_CFI_INCLUDE_DIR}")
    file(COPY "${_GFORTRAN_BINDING_H}" DESTINATION "${_CFI_INCLUDE_DIR}")
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-isystem ${_CFI_INCLUDE_DIR}>")
  endif()
endif()

add_subdirectory(src)

# ---------------------------------------------------------
# Dependencies
# ---------------------------------------------------------
if (NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install)
endif()

target_include_directories(caffeine PUBLIC ${CMAKE_BINARY_DIR}/include)
target_include_directories(caffeine PUBLIC ${CMAKE_SOURCE_DIR}/include)

# --------
# Assert
# --------
SET(ASSERT_GIT "https://github.com/berkeleylab/assert.git")
SET(ASSERT_TAG "3.0.2")
set(_ASSERT_FPM_FLAGS "")
if(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
  set(_ASSERT_FPM_FLAGS "--flag;-fcoarray=single")
endif()
if (USE_FPM)
  set(ASSERT_BUILD_COMMAND fpm build --compiler ${CMAKE_Fortran_COMPILER} ${_ASSERT_FPM_FLAGS})
  set(ASSERT_INSTALL_COMMAND 
        fpm install --prefix=<INSTALL_DIR> --compiler ${CMAKE_Fortran_COMPILER} ${_ASSERT_FPM_FLAGS} &&
        ${CMAKE_COMMAND} -E make_directory <INSTALL_DIR> &&
        ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/include <INSTALL_DIR>/include
  ) 
else()
  message(STATUS "Since USE_FPM is not enabled, libassert will be installed manually from source."
                 "If you want to use Assert's build system, please enable USE_FPM.")
  separate_arguments(_FORTRAN_FLAGS NATIVE_COMMAND "${CMAKE_Fortran_FLAGS}")
  set(ASSERT_BUILD_COMMAND ${CMAKE_Fortran_COMPILER} -c <SOURCE_DIR>/src/assert_m.F90 
        -I<SOURCE_DIR>/include
        -DASSERT_MULTI_IMAGE=0
        -fPIC
        ${_FORTRAN_FLAGS}
  )
  set(ASSERT_INSTALL_COMMAND 
        ${CMAKE_COMMAND} -E make_directory <INSTALL_DIR>/lib &&
        ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/include <INSTALL_DIR>/include &&
        ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/assert_m.mod <INSTALL_DIR>/include &&
        ar rcs <INSTALL_DIR>/lib/libassert.a <SOURCE_DIR>/assert_m.o
  )
endif()
ExternalProject_Add(assert_lib 
  GIT_REPOSITORY ${ASSERT_GIT} 
  GIT_TAG ${ASSERT_TAG}
  CONFIGURE_COMMAND ""
  INSTALL_DIR ${CMAKE_BINARY_DIR}
  BUILD_COMMAND ${ASSERT_BUILD_COMMAND} 
  INSTALL_COMMAND ${ASSERT_INSTALL_COMMAND}
  BUILD_IN_SOURCE 1
)

add_library(assert_lib_external STATIC IMPORTED)
set_target_properties(assert_lib_external PROPERTIES
    IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/lib/libassert.a
    INTERFACE_INCLUDE_DIRECTORIES $<INSTALL_INTERFACE:${CMAKE_BINARY_DIR}/include>
)
add_dependencies(assert_lib_external assert_lib)
target_link_libraries(caffeine PRIVATE assert_lib_external)

# -----------
# GASNet-EX
# -----------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(GASNET_CONFIGURE_ARGS "" CACHE STRING "Arguments to prefix to GASNet-EX configure command (if used).")
if (NOT DEFINED GASNet_THREADING)
  set(GASNet_THREADING seq)
endif()

# The default conduit is smp
# TODO: check against an allow list, and ensure lower-case
set(GASNET_CONDUIT "smp" CACHE STRING "Which GASNet-EX conduit (network) to use")

set(GASNet_PREFERRED_CONDUITS ${GASNET_CONDUIT})
find_package(GASNet QUIET)
if (NOT GASNet_FOUND)
  set(GASNet_TAG "2025.8.0")
  set(GASNet_MD5 "7f61c16c6d06e9789157b3b6804e1927")
  message(STATUS "Cannot find GASNet-Ex, maybe GASNet_ROOT variable is not set.")
  message(STATUS "Configuration and building of GASNet-Ex from release ${GASNet_TAG}.")
  if(BUILD_SHARED_LIBS)
    if(NOT CMAKE_C_FLAGS MATCHES "-fPIC")
      set(CMAKE_C_FLAGS "-fPIC ${CMAKE_C_FLAGS}")
    endif()
    if(NOT CMAKE_CXX_FLAGS MATCHES "-fPIC")
      set(CMAKE_CXX_FLAGS "-fPIC ${CMAKE_CXX_FLAGS}")
    endif()
  endif()

  if(DEFINED GASNET_CONFIGURE_ARGS)
    separate_arguments(GASNET_CONFIGURE_LIST NATIVE_COMMAND "${GASNET_CONFIGURE_ARGS}")
  else()
    unset(GASNET_CONFIGURE_LIST)
  endif()
  
  ExternalProject_Add(gasnet_ex
      URL https://gasnet.lbl.gov/EX/GASNet-${GASNet_TAG}.tar.gz
      URL_HASH MD5=${GASNet_MD5}
      INSTALL_DIR ${CMAKE_BINARY_DIR}/gasnet
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      DOWNLOAD_NO_EXTRACT FALSE
      CONFIGURE_COMMAND 
        <SOURCE_DIR>/configure ${GASNET_CONFIGURE_LIST}
                               --prefix=<INSTALL_DIR>
                               --enable-${GASNET_CONDUIT}
                               --enable-seq --disable-par --disable-parsync
                               --disable-segment-everything
                               --disable-mpi-compat
                               --with-cflags=${CMAKE_C_FLAGS}
                               --with-cxxflags=${CMAKE_CXX_FLAGS}
                               --with-mpi-cflags=${CMAKE_C_FLAGS}
                               --with-cc=${CMAKE_C_COMPILER}
                               --with-cxx=${CMAKE_CXX_COMPILER}
      BUILD_COMMAND make -j
      INSTALL_COMMAND make install
      BUILD_IN_SOURCE 1
  )
  set(GASNET_INSTALL ${CMAKE_BINARY_DIR}/gasnet)

  # Generate "response files" with GASNet's (conduit-specific) link flags.
  # TODO?: Does this make sense to do for every conduit which was compiled?
  set(GASNET_LDFLAGS_FILE "${GASNET_CONDUIT}-seq-ldflags.rsp")
  set(GASNET_LIBS_FILE    "${GASNET_CONDUIT}-seq-libs.rsp")
  find_program(PKG_CONFIG NAMES pkg-config)
  if(PKG_CONFIG STREQUAL "NOTFOUND")
    message(FATAL_ERROR "pkg-config not found")
  endif()
  set(GASNET_PKG_CONFIG
    "env" "PKG_CONFIG_PATH=${GASNET_INSTALL}/lib/pkgconfig"
    "${PKG_CONFIG}" "gasnet-${GASNET_CONDUIT}-seq"
  )
  ExternalProject_Add_Step(
    gasnet_ex dump-vars
    DEPENDEES install
    COMMAND ${GASNET_PKG_CONFIG} --variable=GASNET_LDFLAGS > "${GASNET_INSTALL}/lib/${GASNET_LDFLAGS_FILE}"
    COMMAND ${GASNET_PKG_CONFIG} --variable=GASNET_LIBS    > "${GASNET_INSTALL}/lib/${GASNET_LIBS_FILE}"
  )

  add_library(gasnet_lib STATIC IMPORTED)
  string(TOUPPER ${GASNET_CONDUIT} _conduit_uppercase)
  set_target_properties(gasnet_lib PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS "GASNET_CONDUIT_${_conduit_uppercase};GASNET_SEQ"
    IMPORTED_LOCATION ${GASNET_INSTALL}/lib/libgasnet-${GASNET_CONDUIT}-seq.a
    INTERFACE_INCLUDE_DIRECTORIES $<INSTALL_INTERFACE:${GASNET_INSTALL}/include>
  )
  add_dependencies(gasnet_lib gasnet_ex)

  target_link_options(caffeine PUBLIC
     "$<BUILD_INTERFACE:@${GASNET_INSTALL}/lib/${GASNET_LDFLAGS_FILE}>"
     "$<INSTALL_INTERFACE:@${CMAKE_INSTALL_PREFIX}/gasnet/lib/${GASNET_LDFLAGS_FILE}>"
  )
  target_link_libraries(caffeine PUBLIC gasnet_lib)
  target_link_libraries(caffeine PUBLIC
    "$<BUILD_INTERFACE:-Wl,@${GASNET_INSTALL}/lib/${GASNET_LIBS_FILE}>"
    "$<INSTALL_INTERFACE:-Wl,@${CMAKE_INSTALL_PREFIX}/gasnet/lib/${GASNET_LIBS_FILE}>"
  )
  target_include_directories(caffeine PUBLIC ${GASNET_INSTALL}/include)
  target_include_directories(caffeine PUBLIC ${GASNET_INSTALL}/include/${GASNET_CONDUIT}-conduit)

  install(DIRECTORY "${GASNET_INSTALL}/" DESTINATION "gasnet")
else()
  target_link_libraries(caffeine PUBLIC GASNet::GASNet)
endif()

# ---------------------------------------------------------
# Installation 
# ---------------------------------------------------------
install(TARGETS caffeine
  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

# ---------------------------------------------------------
# Julienne (test framework dependency)
# ---------------------------------------------------------
SET(JULIENNE_GIT "https://github.com/BerkeleyLab/julienne.git")
SET(JULIENNE_TAG "3.6.2")

set(_JULIENNE_FPM_FLAGS "")
if(CMAKE_Fortran_COMPILER_ID STREQUAL "LFortran")
  set(_JULIENNE_FPM_FLAGS "--flag;--cpp;--flag;--separate-compilation;--flag;--realloc-lhs-arrays")
elseif(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
  set(_JULIENNE_FPM_FLAGS "--flag;-fcoarray=single")
endif()

ExternalProject_Add(julienne_lib
  GIT_REPOSITORY ${JULIENNE_GIT}
  GIT_TAG ${JULIENNE_TAG}
  CONFIGURE_COMMAND ""
  INSTALL_DIR ${CMAKE_BINARY_DIR}
  BUILD_COMMAND fpm build --compiler ${CMAKE_Fortran_COMPILER} ${_JULIENNE_FPM_FLAGS}
  INSTALL_COMMAND fpm install --prefix=<INSTALL_DIR> --compiler ${CMAKE_Fortran_COMPILER} ${_JULIENNE_FPM_FLAGS}
  BUILD_IN_SOURCE 1
)

add_library(julienne_lib_external STATIC IMPORTED)
set_target_properties(julienne_lib_external PROPERTIES
    IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/lib/libjulienne.a
    INTERFACE_INCLUDE_DIRECTORIES $<INSTALL_INTERFACE:${CMAKE_BINARY_DIR}/include>
)
add_dependencies(julienne_lib_external julienne_lib)

# ---------------------------------------------------------
# Test using App 
# ---------------------------------------------------------
if(CMAKE_Fortran_COMPILER_ID STREQUAL "LLVMFlang" OR CMAKE_Fortran_COMPILER_ID STREQUAL "LFortran" OR CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
  # If you want to view the test logs, please run : make test ARGS="--verbose"
  enable_testing()
  add_executable(test_native_multi_image app/native-multi-image.F90)
  
  if(CMAKE_Fortran_COMPILER_ID STREQUAL "LLVMFlang")
    target_compile_options(test_native_multi_image PRIVATE "-fcoarray" "-cpp" "-DHAVE_MULTI_IMAGE")
  elseif(CMAKE_Fortran_COMPILER_ID STREQUAL "LFortran")
    target_compile_options(test_native_multi_image PRIVATE "--cpp" "-DHAVE_MULTI_IMAGE" "-DHAVE_TEAM=0" "-DHAVE_SYNC_MEMORY=0" "-DHAVE_SYNC_IMAGES=0" "-DHAVE_COLLECTIVES=0")
  elseif(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_native_multi_image PRIVATE "-fcoarray=single" "-cpp" "-DHAVE_MULTI_IMAGE" "-DHAVE_TEAM=0")
  endif()
  target_link_libraries(test_native_multi_image PRIVATE caffeine)
  
  add_test(NAME TestNativeMultiImage COMMAND test_native_multi_image)

  # ---------------------------------------------------------
  # Julienne-based test suite
  # ---------------------------------------------------------
  set(TEST_SOURCES
    test/julienne-driver.F90
    test/prif_init_test.F90
    test/prif_num_images_test.F90
    test/prif_this_image_test.F90
    test/prif_image_queries_test.F90
    test/prif_types_test.F90
    test/prif_co_broadcast_test.F90
    test/prif_co_sum_test.F90
    test/prif_co_max_test.F90
    test/prif_co_min_test.F90
    test/prif_co_reduce_test.F90
    test/prif_allocate_test.F90
    test/prif_coarray_inquiry_test.F90
    test/prif_sync_images_test.F90
    test/prif_image_index_test.F90
    test/prif_teams_test.F90
    test/prif_rma_test.F90
    test/prif_strided_test.F90
    test/prif_event_test.F90
    test/prif_atomic_test.F90
    test/prif_error_stop_test.F90
    test/prif_stop_test.F90
  )

  add_executable(test_julienne_suite ${TEST_SOURCES})
  add_dependencies(test_julienne_suite julienne_lib)

  if(CMAKE_Fortran_COMPILER_ID STREQUAL "LLVMFlang")
    target_compile_options(test_julienne_suite PRIVATE "-fcoarray" "-cpp")
  elseif(CMAKE_Fortran_COMPILER_ID STREQUAL "LFortran")
    target_compile_options(test_julienne_suite PRIVATE "--cpp" "-DJULIENNE_PARALLEL_CALLBACKS=0")
  elseif(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_julienne_suite PRIVATE "-fcoarray=single" "-cpp")
  endif()

  target_include_directories(test_julienne_suite PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_BINARY_DIR}/src/caffeine
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/julienne_lib-prefix/src/julienne_lib/include
  )
  target_link_libraries(test_julienne_suite PRIVATE caffeine julienne_lib_external)

  add_test(NAME TestJulienneSuite COMMAND test_julienne_suite)
else()
  message(STATUS "${CMAKE_Fortran_COMPILER_ID} doesn't support lowering to PRIF, test can't be built.")
endif()
