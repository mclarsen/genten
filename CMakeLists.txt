#@HEADER
# ************************************************************************
#     Genten: Software for Generalized Tensor Decompositions
#     by Sandia National Laboratories
#
# Sandia National Laboratories is a multimission laboratory managed
# and operated by National Technology and Engineering Solutions of Sandia,
# LLC, a wholly owned subsidiary of Honeywell International, Inc., for the
# U.S. Department of Energy's National Nuclear Security Administration under
# contract DE-NA0003525.
#
# Copyright 2017 National Technology & Engineering Solutions of Sandia, LLC
# (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
# Government retains certain rights in this software.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# 1. Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# ************************************************************************
#@HEADER


#---- Specify the CMake version required.  Kokkos requires 3.16
CMAKE_MINIMUM_REQUIRED (VERSION 3.16)

#---- Declaring the project name will define ${Genten_SOURCE_DIR} to be
#---- the path to this source file, and ${Genten_BINARY_DIR} to be the path
#---- to the directory where cmake has been invoked.
project(Genten VERSION "0.0.0")

#---- Require C++14 without GNU extensions
SET(CMAKE_CXX_STANDARD 14)
SET(CMAKE_CXX_EXTENSIONS OFF)

#------------------------------------------------------------
#---- Package OPTIONS
#------------------------------------------------------------
# Note: OPTION is a binary thing and can't have string in them
OPTION(BUILD_SHARED_LIBS "Build shared libraries." OFF)
OPTION(ENABLE_BOOST "Enable Boost for reading compressed tensor files" OFF)
OPTION(ENABLE_CALIPER "Enable Caliper for application profiling" OFF)
OPTION(ENABLE_MATLAB "Enable MATLAB linkage to Tensor Toolbox" OFF)
OPTION(FLOAT_TYPE "C++ data type used for floating-point values" OFF)
OPTION(INDEX_TYPE "C++ data type used for tensor indices" OFF)
OPTION(ENABLE_GCP "Enable experimental GCP code" ON)
OPTION(ENABLE_TESTS "Enable tests" OFF)

IF(FLOAT_TYPE)
  SET(GENTEN_FLOAT_TYPE ${FLOAT_TYPE})
ELSE()
  SET(GENTEN_FLOAT_TYPE "double")
ENDIF()

IF(INDEX_TYPE)
  SET(GENTEN_INDEX_TYPE ${INDEX_TYPE})
ELSE()
  SET(GENTEN_INDEX_TYPE "size_t")
ENDIF()

# if the kokkos path is provided use the installation
IF(KOKKOS_PATH)
  SET(KOKKOS_INLINE_BUILD OFF)
ELSE()
  SET(KOKKOS_INLINE_BUILD ON)
ENDIF()

# Check minimum compiler version
INCLUDE (cmake/CheckCompilerVersion.cmake)

# If we are doing an inline build of Kokkos
IF(KOKKOS_INLINE_BUILD)
  message(STATUS "enabling kokkos inline ************************************")
  # We must enable Lambda support in Kokkos
  IF(Kokkos_ENABLE_CUDA)
    SET(Kokkos_ENABLE_CUDA_LAMBDA ON CACHE BOOL "Enable lambdas for CUDA.")
  ENDIF()

  # Configure kokkos
  ADD_SUBDIRECTORY(tpls/kokkos)

ELSE()

  # Link against installed Kokkos
  FIND_PACKAGE(Kokkos REQUIRED PATHS ${KOKKOS_PATH} NO_DEFAULT_PATH)
  MESSAGE("-- Genten:  Using Kokkos:  ${Kokkos_CONFIG}")

ENDIF()
SET(HAVE_KOKKOS ON)

SET(GENTEN_LIBS "")

IF (ENABLE_BOOST)
  OPTION(BOOST_PATH "Path to BOOST installation")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem ${BOOST_PATH}/include")
  SET(GENTEN_LIBS "${GENTEN_LIBS} -L${BOOST_PATH}/lib -lboost_iostreams -lz")
  SET(HAVE_BOOST ${ENABLE_BOOST})
ENDIF()

IF (ENABLE_CALIPER)
  OPTION(CALIPER_PATH "Path to Caliper installation")
  OPTION(LIBPFM_PATH "Path to libpfm installation")
  OPTION(LIBUNWIND_PATH "Path to libunwind installation")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem ${CALIPER_PATH}/include")
  SET(GENTEN_LIBS "${GENTEN_LIBS} -L${CALIPER_PATH}/lib64 -lcaliper -lcaliper-common -lcaliper-reader -lgotcha")
  IF (LIBPFM_PATH)
    SET(GENTEN_LIBS "${GENTEN_LIBS} -L${LIBPFM_PATH}/lib -lpfm")
  ENDIF()
  IF (LIBUNWIND_PATH)
    SET(GENTEN_LIBS "${GENTEN_LIBS} -L${LIBUNWIND_PATH}/lib -lunwind")
  ENDIF()
  SET(HAVE_CALIPER ${ENABLE_CALIPER})
ENDIF()

SET(ROL_LIBRARIES "")
SET(ROL_TPL_LIBRARIES "")
IF (ENABLE_GCP)
  SET(HAVE_GCP ${ENABLE_GCP})
  OPTION(ENABLE_ROL "Enable ROL for GCP optimization" OFF)
  IF (ENABLE_ROL)
    OPTION(ROL_PATH "Path to ROL installation")
    FIND_PACKAGE(ROL PATHS "${ROL_PATH}" NO_DEFAULT_PATH)

    MESSAGE("\nGenten:  Found ROL: ")
    MESSAGE("   ROL_DIR = ${ROL_DIR}")
    MESSAGE("   ROL_PACKAGE_LIST = ${ROL_PACKAGE_LIST}")
    MESSAGE("   ROL_LIBRARIES = ${ROL_LIBRARIES}")
    MESSAGE("   ROL_INCLUDE_DIRS = ${ROL_INCLUDE_DIRS}")
    MESSAGE("   ROL_LIBRARY_DIRS = ${ROL_LIBRARY_DIRS}")
    MESSAGE("   ROL_TPL_LIST = ${ROL_TPL_LIST}")
    MESSAGE("   ROL_TPL_INCLUDE_DIRS = ${ROL_TPL_INCLUDE_DIRS}")
    MESSAGE("   ROL_TPL_LIBRARIES = ${ROL_TPL_LIBRARIES}")
    MESSAGE("   ROL_TPL_LIBRARY_DIRS = ${ROL_TPL_LIBRARY_DIRS}")

    # Use SYSTEM keyword so that output is not littered with ROL warnings
    INCLUDE_DIRECTORIES(SYSTEM ${ROL_INCLUDE_DIRS} ${ROL_TPL_INCLUDE_DIRS})
    LINK_DIRECTORIES(${ROL_LIBRARY_DIRS} ${ROL_TPL_LIBRARY_DIRS} )
    SET(HAVE_ROL ${ENABLE_ROL})
  ENDIF()
ENDIF()

IF(ENABLE_MATLAB)
  IF(NOT BUILD_SHARED_LIBS)
    MESSAGE(FATAL_ERROR "Must build shared libraries when enabling Matlab!")
  ENDIF()
  OPTION(MATLAB_PATH "Path to MATLAB installation")
  SET(Matlab_ROOT_DIR "${MATLAB_PATH}")
  FIND_PACKAGE(Matlab COMPONENTS MX_LIBRARY)
  IF(Matlab_FOUND)
    SET(HAVE_MATLAB ON)
  ELSE()
    MESSAGE(FATAL_ERROR "Could not find MATLAB!")
  ENDIF()
ENDIF()

IF(Kokkos_ENABLE_CUDA)
  OPTION(ENABLE_CUBLAS "Flag to enable cuBLAS for Kokkos with Cuda (do not turn this off unless you know what you are doing)" ON)
  OPTION(ENABLE_CUSOLVER "Flag to enable cuSolver for Kokkos with Cuda (do not turn this off unless you know what you are doing)" ON)
  find_package(CUDA)
  IF (ENABLE_CUBLAS)
    list(APPEND GENTEN_LIBS ${CUDA_CUBLAS_LIBRARIES})
    SET(HAVE_CUBLAS ${ENABLE_CUBLAS})
    MESSAGE (STATUS "GenTen: Using cuBLAS ${CUDA_CUBLAS_LIBRARIES}")
  ENDIF()
  IF (ENABLE_CUSOLVER)
    list(APPEND GENTEN_LIBS ${CUDA_cusolver_LIBRARY})
    SET(HAVE_CUSOLVER ${ENABLE_CUSOLVER})
    MESSAGE (STATUS "GenTen: Using cuSolver ${CUDA_cusolver_LIBRARY}")
  ENDIF()
  # Add useful CXX flags when building with Cuda
  message(STATUS "bananas")
#SET(CMAKE_CUDA_FLAGS "${CMAKE_CXX_FLAGS} --expt-relaxed-constexpr")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --expt-relaxed-constexpr")
  IF (KOKKOS_CXX_COMPILER_ID STREQUAL NVIDIA)
    message(STATUS "more bananas")
    SET(CMAKE_CUDA_FLAGS "${CMAKE_CXX_FLAGS} -Xcudafe --diag_suppress=code_is_unreachable")
    #message("CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")
  ENDIF()
ENDIF()

# Strip leading/trailing whitespace from GENTEN_LIBS
STRING(STRIP "${GENTEN_LIBS}" GENTEN_LIBS)

#------------------------------------------------------------
#---- Output Directories
#------------------------------------------------------------
#---- Put all the executables in a /bin subdirectory.
SET (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${Genten_BINARY_DIR}/bin)

#---- Put all the libraries in a /lib subdirectory.
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${Genten_BINARY_DIR}/lib)
SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${Genten_BINARY_DIR}/lib)

#--- Use standard GNU installation directories
INCLUDE(GNUInstallDirs)

#------------------------------------------------------------
#---- INCLUDE Directories
#------------------------------------------------------------
#---- Configure the build type (debug or production).
INCLUDE (cmake/ConfigureBuildType.cmake)

#---- Define the build location as the default install directory.
IF (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  SET (CMAKE_INSTALL_PREFIX build)
ENDIF (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

#---- Define system-dependent C++ preprocessing macros,
#---- and copy to the build directory.
CONFIGURE_FILE (
  ${Genten_SOURCE_DIR}/cmake/CMakeInclude.h.cmake
  ${Genten_BINARY_DIR}/CMakeInclude/CMakeInclude.h
  )

set(config_file ${Genten_BINARY_DIR}/CMakeInclude/CMakeInclude.h)

#------------------------------------------------------------
#---- INCLUDE Directories
#------------------------------------------------------------
INCLUDE_DIRECTORIES (
  ${Kokkos_INCLUDE_DIRS_RET}
  ${Genten_SOURCE_DIR}/src/
  ${Genten_SOURCE_DIR}/src/mathlib/
  ${Genten_SOURCE_DIR}/src/rol/
  ${Genten_BINARY_DIR}/CMakeInclude/
  ${Genten_SOURCE_DIR}/driver/
  )
FILE (GLOB genten_headers
  ${Genten_SOURCE_DIR}/src/*.h
  ${Genten_SOURCE_DIR}/src/*.hpp
  ${Genten_SOURCE_DIR}/src/mathlib/*.h
  ${Genten_SOURCE_DIR}/src/mathlib/*.hpp
  ${Genten_SOURCE_DIR}/src/rol*.hpp
  )
INSTALL (
  FILES ${genten_headers} ${config_file}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/genten
  PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
  )


#------------------------------------------------------------
#---- Test Data
#------------------------------------------------------------
#---- Copy test data.
ADD_CUSTOM_TARGET (copy_data_dir ALL)
ADD_CUSTOM_COMMAND (
  TARGET copy_data_dir POST_BUILD
  COMMAND cmake -E copy_directory
  ${Genten_SOURCE_DIR}/data
  ${Genten_BINARY_DIR}/data
  )
INSTALL (
  DIRECTORY ${Genten_SOURCE_DIR}/data
  DESTINATION ${CMAKE_INSTALL_DATADIR}/genten
  DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ GROUP_EXECUTE
                        WORLD_READ WORLD_EXECUTE
  FILE_PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
  )

#---- The location of test data is unfortunately hard-coded into source code.
#---- The CMake directive below puts the relative location into source, but it
#---- causes strange compiler errors on Windows.
#---- ADD_DEFINITIONS (-DDATADIR=\"${Genten_SOURCE_DIR}/data/\")


#------------------------------------------------------------
#---- LAPACK Math Library
# Separate pure c code from c++ code so CMake doesn't add Kokkos CXX flags
# to compile of c code (since the c code can't depend on kokkos)
#------------------------------------------------------------
OPTION(ENABLE_LAPACK "Enable BLAS/LAPACK.  Only required for CPU (cuBLAS/cuSolver are used on GPU)." ON)
INCLUDE (cmake/ConfigureLapack.cmake)
IF (LAPACK_FOUND)
  ADD_DEFINITIONS (-DLAPACK_FOUND)
  ADD_LIBRARY (genten_mathlibs_c
    ${Genten_SOURCE_DIR}/src/mathlib/Genten_Default_VML.c
    )
ELSE (LAPACK_FOUND)
  #---- Tell CMake to build with default BLAS implementation.
  ADD_LIBRARY (genten_mathlibs_c
    ${Genten_SOURCE_DIR}/src/mathlib/Genten_Default_Blas.c
    ${Genten_SOURCE_DIR}/src/mathlib/Genten_Default_VML.c
    )
ENDIF (LAPACK_FOUND)
TARGET_LINK_LIBRARIES(
  genten_mathlibs_c
  ${LAPACK_LIBS} ${LAPACK_ADD_LIBS}
  )
INSTALL (
  TARGETS genten_mathlibs_c
  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  EXPORT genten
  )
SET(GENTEN_MATHLIBS
  genten_mathlibs_c
  )
TARGET_LINK_LIBRARIES(
  ${GENTEN_MATHLIBS} ${LAPACK_LIBS} ${LAPACK_ADD_LIBS}
  )


#------------------------------------------------------------
#---- Genten Library
#------------------------------------------------------------

#---- List the source files to be built.
SET (Genten_SOURCE_LIST
  ${Genten_SOURCE_DIR}/src/mathlib/Genten_DiscreteCDF.cpp
  ${Genten_SOURCE_DIR}/src/mathlib/Genten_FacTestSetGenerator.cpp
  ${Genten_SOURCE_DIR}/src/mathlib/Genten_RandomMT.cpp
  ${Genten_SOURCE_DIR}/src/mathlib/Genten_SystemTimer.cpp
  ${Genten_SOURCE_DIR}/src/Genten_Util.cpp
  ${Genten_SOURCE_DIR}/src/Genten_AlgParams.cpp
  ${Genten_SOURCE_DIR}/src/Genten_Array.cpp
  ${Genten_SOURCE_DIR}/src/Genten_CpAls.cpp
  ${Genten_SOURCE_DIR}/src/Genten_FacMatArray.cpp
  ${Genten_SOURCE_DIR}/src/Genten_FacMatrix.cpp
  ${Genten_SOURCE_DIR}/src/Genten_IndxArray.cpp
  ${Genten_SOURCE_DIR}/src/Genten_IOtext.cpp
  ${Genten_SOURCE_DIR}/src/Genten_Ktensor.cpp
  ${Genten_SOURCE_DIR}/src/Genten_MixedFormatOps.cpp
  ${Genten_SOURCE_DIR}/src/Genten_TTM.cpp
  ${Genten_SOURCE_DIR}/src/Genten_MathLibs_Wpr.cpp
  ${Genten_SOURCE_DIR}/src/Genten_portability.cpp
  ${Genten_SOURCE_DIR}/src/Genten_Sptensor.cpp
  ${Genten_SOURCE_DIR}/src/Genten_Tensor.cpp
  ${Genten_SOURCE_DIR}/src/Genten_Driver.cpp
  )

IF (ENABLE_GCP)
  SET (Genten_SOURCE_LIST ${Genten_SOURCE_LIST}
    ${Genten_SOURCE_DIR}/src/Genten_GCP_ValueKernels.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SamplingKernels.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SGD.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SGD_SA.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SS_Grad_Gaussian.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SS_Grad_Poisson.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SS_Grad_Rayleigh.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SS_Grad_Gamma.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SS_Grad_Bernoulli.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SS_Grad_SA_Gaussian.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SS_Grad_SA_Poisson.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SS_Grad_SA_Rayleigh.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SS_Grad_SA_Gamma.cpp
    ${Genten_SOURCE_DIR}/src/Genten_GCP_SS_Grad_SA_Bernoulli.cpp
    )

  IF (ENABLE_ROL)
    SET (Genten_SOURCE_LIST ${Genten_SOURCE_LIST}
      ${Genten_SOURCE_DIR}/src/rol/Genten_GCP_Opt.cpp
      )
  ENDIF()
ENDIF()


#---- Tell CMake to build the library.
ADD_LIBRARY (gentenlib ${Genten_SOURCE_LIST})
TARGET_LINK_LIBRARIES(gentenlib Kokkos::kokkos)
SET(GENTEN_LINK_LIBS gentenlib ${GENTEN_MATHLIBS} ${GENTEN_LIBS} ${ROL_LIBRARIES} ${LAPACK_LIBS} ${LAPACK_ADD_LIBS} ${OPSYS_LIBRARIES})
TARGET_LINK_LIBRARIES(${GENTEN_LINK_LIBS})
INSTALL (
  TARGETS gentenlib
  EXPORT genten
  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )

#---- Adding the higher moment interface to the Library sources
#---- Must be a better way to do this. Inside an IF??
ADD_SUBDIRECTORY(joint_moments)

# FIND_PACKAGE(GTest REQUIRED)
# IF (${GTest_FOUND})
#   MESSAGE(STATUS "FOUND GTEST")
#   MESSAGE(STATUS "GTEST_INCLUDE_DIR: " ${GTEST_INCLUDE_DIR})
# ELSE (${GTest_FOUND})
#   MESSAGE(STATUS "DID NOT FIND GTEST")
# ENDIF (${GTest_FOUND})
# include_directories(${GTEST_INCLUDE_DIR})

#------------------------------------------------------------
#---- Unit Test Executables (not installed)
#------------------------------------------------------------

#---- Specifications for building unit_tests.

if(ENABLE_TESTS)
  SET(UNIT_TEST_SRCS
    ${Genten_SOURCE_DIR}/test/Genten_Test_TTM.cpp
    ${Genten_SOURCE_DIR}/test/Genten_Test_Array.cpp
    ${Genten_SOURCE_DIR}/test/Genten_Test_CpAls.cpp
    ${Genten_SOURCE_DIR}/test/Genten_Test_FacMatrix.cpp
    ${Genten_SOURCE_DIR}/test/Genten_Test_IndxArray.cpp
    ${Genten_SOURCE_DIR}/test/Genten_Test_IOtext.cpp
    ${Genten_SOURCE_DIR}/test/Genten_Test_Ktensor.cpp
    ${Genten_SOURCE_DIR}/test/Genten_Test_MixedFormats.cpp
    ${Genten_SOURCE_DIR}/test/Genten_Test_Sptensor.cpp
    ${Genten_SOURCE_DIR}/test/Genten_Test_Tensor.cpp
    ${Genten_SOURCE_DIR}/test/Genten_Test_UnitTests.cpp
    ${Genten_SOURCE_DIR}/test/Genten_Test_Utils.cpp
    )
  IF (ENABLE_GCP)
     SET(UNIT_TEST_SRCS ${UNIT_TEST_SRCS}
       ${Genten_SOURCE_DIR}/test/Genten_Test_GCP_SGD.cpp
       )
    IF(ENABLE_ROL)
      SET(UNIT_TEST_SRCS ${UNIT_TEST_SRCS}
        ${Genten_SOURCE_DIR}/test/Genten_Test_GCP_Opt.cpp
        )
    ENDIF()
  ENDIF()

  ADD_EXECUTABLE (
    unit_tests
    ${UNIT_TEST_SRCS}
    )
  TARGET_LINK_LIBRARIES (unit_tests ${GENTEN_LINK_LIBS})
endif()


#------------------------------------------------------------
#---- Performance Test Executables (not installed)
#------------------------------------------------------------
if(ENABLE_TESTS)
  ADD_EXECUTABLE (
    perf_CpAlsAminoAcid
    ${Genten_SOURCE_DIR}/performance/Genten_CpAlsAminoAcid.cpp
    )
  TARGET_LINK_LIBRARIES (perf_CpAlsAminoAcid ${GENTEN_LINK_LIBS})

  ADD_EXECUTABLE (
    perf_CpAlsRandomKtensor
    ${Genten_SOURCE_DIR}/performance/Genten_CpAlsRandomKtensor.cpp
    ${Genten_SOURCE_DIR}/src/mathlib/Genten_DiscreteCDF.cpp
    ${Genten_SOURCE_DIR}/src/mathlib/Genten_FacTestSetGenerator.cpp
    )
  TARGET_LINK_LIBRARIES (perf_CpAlsRandomKtensor ${GENTEN_LINK_LIBS})

  ADD_EXECUTABLE (
    perf_CpAlsRandomKtensor_Sweep
    ${Genten_SOURCE_DIR}/performance/Genten_CpAlsRandomKtensor_Sweep.cpp
    ${Genten_SOURCE_DIR}/src/mathlib/Genten_DiscreteCDF.cpp
    ${Genten_SOURCE_DIR}/src/mathlib/Genten_FacTestSetGenerator.cpp
    )
  TARGET_LINK_LIBRARIES (perf_CpAlsRandomKtensor_Sweep ${GENTEN_LINK_LIBS})

  ADD_EXECUTABLE (
    perf_MTTKRP
    ${Genten_SOURCE_DIR}/performance/Genten_MTTKRP.cpp
    ${Genten_SOURCE_DIR}/src/mathlib/Genten_DiscreteCDF.cpp
    ${Genten_SOURCE_DIR}/src/mathlib/Genten_FacTestSetGenerator.cpp
    )
  TARGET_LINK_LIBRARIES (perf_MTTKRP ${GENTEN_LINK_LIBS})

  ADD_EXECUTABLE (
    perf_MTTKRP_Sweep
    ${Genten_SOURCE_DIR}/performance/Genten_MTTKRP_Sweep.cpp
    ${Genten_SOURCE_DIR}/src/mathlib/Genten_DiscreteCDF.cpp
    ${Genten_SOURCE_DIR}/src/mathlib/Genten_FacTestSetGenerator.cpp
    )
  TARGET_LINK_LIBRARIES (perf_MTTKRP_Sweep ${GENTEN_LINK_LIBS})
endif()

#------------------------------------------------------------
#---- Drivers
#------------------------------------------------------------

#---- Specifications for building each performance test.
#---- In each case specify the executable and its link library dependencies.

#matt
if(FALSE)
  ADD_EXECUTABLE (
    genten
    ${Genten_SOURCE_DIR}/driver/Genten_Driver.cpp
    )
  TARGET_LINK_LIBRARIES (genten ${GENTEN_LINK_LIBS})
  INSTALL (
    TARGETS genten
    EXPORT genten
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

#------------------------------------------------------------
#---- Matlab
#------------------------------------------------------------

# Matlab Tensor Toolbox stores floats as doubles, so the interface will
# only compile if ttb_real == double
IF(HAVE_MATLAB AND (${GENTEN_FLOAT_TYPE} STREQUAL "double"))

  ADD_LIBRARY(
    gt_matlab
    ${Genten_SOURCE_DIR}/matlab/Genten_Matlab.hpp
    ${Genten_SOURCE_DIR}/matlab/Genten_Matlab.cpp)
  TARGET_INCLUDE_DIRECTORIES(gt_matlab PRIVATE ${Matlab_INCLUDE_DIRS})
  TARGET_LINK_LIBRARIES(gt_matlab ${GENTEN_LINK_LIBS} ${Matlab_MEX_LIBRARY}
    ${Matlab_MX_LIBRARY})
  SET(GENTEN_MATLAB_LINK_LIBS gt_matlab ${GENTEN_LINK_LIBS})

  # Newer versions of CMake have an API version argument to MATLAB_ADD_MEX
  # which needs to be set to the right value since it defaults to an incorrect
  # value
  SET(GENTEN_MATLAB_API_VERSION "")
  IF(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.14.0")
    SET(GENTEN_MATLAB_API_VERSION "R2018a")
  ENDIF()

  MATLAB_ADD_MEX(
    NAME gt_cp_driver
    SRC ${Genten_SOURCE_DIR}/matlab/Genten_Matlab_Driver.cpp
    ${GENTEN_MATLAB_API_VERSION})
  MATLAB_ADD_MEX(
    NAME gt_initialize
    SRC ${Genten_SOURCE_DIR}/matlab/Genten_Matlab_Initialize.cpp
    ${GENTEN_MATLAB_API_VERSION})
  MATLAB_ADD_MEX(
    NAME gt_finalize
    SRC ${Genten_SOURCE_DIR}/matlab/Genten_Matlab_Finalize.cpp
    ${GENTEN_MATLAB_API_VERSION})
  MATLAB_ADD_MEX(
    NAME gt_compute_perm_driver
    SRC ${Genten_SOURCE_DIR}/matlab/Genten_Matlab_ComputePermutation.cpp
    ${GENTEN_MATLAB_API_VERSION})
  MATLAB_ADD_MEX(
    NAME gt_mttkrp_driver
    SRC ${Genten_SOURCE_DIR}/matlab/Genten_Matlab_MTTKRP.cpp
    ${GENTEN_MATLAB_API_VERSION})
  MATLAB_ADD_MEX(
    NAME gt_innerprod_driver
    SRC ${Genten_SOURCE_DIR}/matlab/Genten_Matlab_Innerprod.cpp
    ${GENTEN_MATLAB_API_VERSION})
  TARGET_LINK_LIBRARIES (gt_cp_driver ${GENTEN_MATLAB_LINK_LIBS})
  TARGET_LINK_LIBRARIES (gt_initialize ${GENTEN_MATLAB_LINK_LIBS})
  TARGET_LINK_LIBRARIES (gt_finalize ${GENTEN_MATLAB_LINK_LIBS})
  TARGET_LINK_LIBRARIES (gt_compute_perm_driver ${GENTEN_MATLAB_LINK_LIBS})
  TARGET_LINK_LIBRARIES (gt_mttkrp_driver ${GENTEN_MATLAB_LINK_LIBS})
  TARGET_LINK_LIBRARIES (gt_innerprod_driver ${GENTEN_MATLAB_LINK_LIBS})
  INSTALL(
    TARGETS gt_matlab gt_cp_driver gt_initialize gt_finalize gt_mttkrp_driver
      gt_innerprod_driver
    DESTINATION ${CMAKE_INSTALL_DATADIR}/genten/matlab)
  INSTALL(
    TARGETS gt_compute_perm_driver
    DESTINATION ${CMAKE_INSTALL_DATADIR}/genten/matlab/@sptensor_gt/private)
  SET_PROPERTY(
    TARGET gt_matlab gt_cp_driver gt_initialize gt_finalize gt_mttkrp_driver
      gt_innerprod_driver
    PROPERTY LIBRARY_OUTPUT_DIRECTORY ${Genten_BINARY_DIR}/matlab)
  SET_PROPERTY(
    TARGET gt_compute_perm_driver
    PROPERTY LIBRARY_OUTPUT_DIRECTORY ${Genten_BINARY_DIR}/matlab/@sptensor_gt/private)

  # Remove -DDLL_EXPORT_SYMBOL=... since it causes problems with nvcc_wrapper
  # Instead we define this inside the source
  SET_PROPERTY(
    TARGET gt_matlab gt_cp_driver gt_initialize gt_finalize
      gt_compute_perm_driver gt_mttkrp_driver gt_innerprod_driver
    PROPERTY DEFINE_SYMBOL "")

  # Targets to install matlab source files
  ADD_CUSTOM_TARGET (copy_matlab_dir ALL)
  ADD_CUSTOM_COMMAND (
    TARGET copy_matlab_dir POST_BUILD
    COMMAND cmake -E copy_directory
    ${Genten_SOURCE_DIR}/matlab/matlab_src
    ${Genten_BINARY_DIR}/matlab
    )
  # Hint:  Use trailing slash in directory name to remove it from install tree
  INSTALL (
    DIRECTORY ${Genten_SOURCE_DIR}/matlab/matlab_src/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/genten/matlab)

ENDIF()

#------------------------------------------------------------
#---- Testing
#------------------------------------------------------------
if(ENABLE_TESTS)
  enable_testing()

# Unit tests -- tests don't return nonzero on failure, so have to grep output
  add_test(Genten_Unit_Tests ${Genten_BINARY_DIR}/bin/unit_tests)
  SET_TESTS_PROPERTIES(Genten_Unit_Tests PROPERTIES FAIL_REGULAR_EXPRESSION "Error")

# mttkrp -- returns nonzero on failure
  add_test(Genten_MTTKRP_random_atomic ${Genten_BINARY_DIR}/bin/perf_MTTKRP --nc 16 --dims [300,400,500] --nnz 1000 --mttkrp-method atomic)
  add_test(Genten_MTTKRP_random_dupl ${Genten_BINARY_DIR}/bin/perf_MTTKRP --nc 16 --dims [300,400,500] --nnz 1000 --mttkrp-method duplicated)
  add_test(Genten_MTTKRP_random_perm ${Genten_BINARY_DIR}/bin/perf_MTTKRP --nc 16 --dims [300,400,500] --nnz 1000 --mttkrp-method perm)
  add_test(Genten_MTTKRP_aminoacid_atomic ${Genten_BINARY_DIR}/bin/perf_MTTKRP --nc 16 --input ${Genten_BINARY_DIR}/data/aminoacid_data.txt --mttkrp-method atomic)
  add_test(Genten_MTTKRP_aminoacid_dupl ${Genten_BINARY_DIR}/bin/perf_MTTKRP --nc 16 --input ${Genten_BINARY_DIR}/data/aminoacid_data.txt --mttkrp-method duplicated)
  add_test(Genten_MTTKRP_aminoacid_perm ${Genten_BINARY_DIR}/bin/perf_MTTKRP --nc 16 --input ${Genten_BINARY_DIR}/data/aminoacid_data.txt --mttkrp-method perm)
  add_test(Genten_MTTKRP_random_dense ${Genten_BINARY_DIR}/bin/perf_MTTKRP --nc 16 --dims [30,40,50] --dense)
endif()
#------------------------------------------------------------
#---- Config
#------------------------------------------------------------
add_subdirectory(config)
install(EXPORT genten DESTINATION lib64/cmake/${PROJECT_NAME})
