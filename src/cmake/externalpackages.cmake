# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

###########################################################################
# Find external dependencies
###########################################################################

# When not in VERBOSE mode, try to make things as quiet as possible
if (NOT VERBOSE)
    set (PkgConfig_FIND_QUIETLY true)
    set (Threads_FIND_QUIETLY true)
endif ()

message (STATUS "${ColorBoldWhite}")
message (STATUS "* Checking for dependencies...")
message (STATUS "*   - Missing a dependency 'Package'?")
message (STATUS "*     Try cmake -DPackage_ROOT=path or set environment var Package_ROOT=path")
message (STATUS "*     For many dependencies, we supply src/build-scripts/build_Package.bash")
message (STATUS "*   - To exclude an optional dependency (even if found),")
message (STATUS "*     -DUSE_Package=OFF or set environment var USE_Package=OFF ")
message (STATUS "${ColorReset}")

# Place where locally built (by src/build-scripts/*) dependencies go.
# Put it first in the prefix path.
set (OSL_LOCAL_DEPS_DIR "${CMAKE_SOURCE_DIR}/ext/dist" CACHE STRING
     "Local area for dependencies added to CMAKE_PREFIX_PATH")
list (INSERT CMAKE_PREFIX_PATH 0 ${OSL_LOCAL_DEPS_DIR})
message (STATUS "CMAKE_PREFIX_PATH = ${CMAKE_PREFIX_PATH}")


include (ExternalProject)

option (BUILD_MISSING_DEPS "Try to download and build any missing dependencies" OFF)

checked_find_package (ZLIB REQUIRED)  # Needed by several packages

# IlmBase & OpenEXR
checked_find_package (Imath REQUIRED
                      VERSION_MIN 3.1
                      PRINT IMATH_INCLUDES Imath_VERSION
                     )
# Force Imath includes to be before everything else to ensure that we have
# the right Imath version, not some older version in the system library.
include_directories(BEFORE ${IMATH_INCLUDES})
set (OSL_USING_IMATH 3)


# OpenImageIO
checked_find_package (OpenImageIO REQUIRED
                      VERSION_MIN 3.0
                      DEFINITIONS OIIO_HIDE_FORMAT=1)

checked_find_package (pugixml REQUIRED
                      VERSION_MIN 1.8)


# LLVM library setup
checked_find_package (LLVM REQUIRED
                      VERSION_MIN 14.0
                      VERSION_MAX 23.9
                      PRINT LLVM_SYSTEM_LIBRARIES CLANG_LIBRARIES
                            LLVM_SHARED_MODE)
# ensure include directory is added (in case of non-standard locations
include_directories (BEFORE SYSTEM "${LLVM_INCLUDES}")
link_directories ("${LLVM_LIB_DIR}")
# Extract and concatenate major & minor, remove wayward patches,
# dots, and "svn" or other suffixes.
string (REGEX REPLACE "([0-9]+)\\.([0-9]+).*" "\\1\\2" OSL_LLVM_VERSION ${LLVM_VERSION})
add_compile_definitions (OSL_LLVM_VERSION=${OSL_LLVM_VERSION})
add_compile_definitions (OSL_LLVM_FULL_VERSION="${LLVM_VERSION}")
if (LLVM_NAMESPACE)
    add_compile_definitions (LLVM_NAMESPACE=${LLVM_NAMESPACE})
endif ()
if (LLVM_VERSION VERSION_GREATER_EQUAL 15.0 AND CMAKE_COMPILER_IS_CLANG
    AND ANY_CLANG_VERSION_STRING VERSION_LESS 15.0)
    message (WARNING
         "${ColorYellow}"
         "If you are using LLVM 15 or higher, you should also use clang version "
         "15 or higher, or you may get build errors.${ColorReset}\n")
endif ()



checked_find_package (partio)


# From pythonutils.cmake. Which binding framework(s) we need is selected by
# OSL_PYTHON_BINDINGS_BACKEND; only look for the ones actually asked for, so
# that e.g. a nanobind-only build doesn't require pybind11 to be installed.
find_python ()
if (USE_PYTHON AND OSL_BUILD_PYTHON_PYBIND11)
    checked_find_package (pybind11 REQUIRED VERSION_MIN 2.7)
endif ()
if (USE_PYTHON AND OSL_BUILD_PYTHON_NANOBIND)
    discover_nanobind_cmake_dir ()
    checked_find_package (nanobind CONFIG REQUIRED
                          VERSION_MIN 2.8.0
                          NO_FP_RANGE_CHECK
                          BUILD_LOCAL missing)
endif ()


# Qt -- used for osltoy
option (USE_QT "Use Qt if found" ON)
if (USE_QT)
    checked_find_package (Qt6 COMPONENTS Core Gui Widgets OpenGLWidgets)
    if (NOT Qt6_FOUND)
        checked_find_package (Qt5 COMPONENTS Core Gui Widgets OpenGL)
    endif ()
    if (NOT Qt5_FOUND AND NOT Qt6_FOUND AND APPLE)
        message (STATUS "  If you think you installed qt with Homebrew and it still doesn't work,")
        message (STATUS "  try:   export PATH=/usr/local/opt/qt/bin:$PATH")
    endif ()
endif ()


# CUDA setup
option (CUDA_PREFER_STATIC_LIBS "Prefer static CUDA libraries" OFF)
if (OSL_USE_OPTIX)
    if (USE_LLVM_BITCODE)
        # FindCUDAToolkit uses CUDAToolkit_ROOT. Honor the old FindCUDA input
        # as a fallback so existing OSL build scripts keep working.
        if (NOT CUDAToolkit_ROOT
            AND "$ENV{CUDAToolkit_ROOT}" STREQUAL "")
            if (CUDA_TOOLKIT_ROOT_DIR)
                set (CUDAToolkit_ROOT "${CUDA_TOOLKIT_ROOT_DIR}")
            elseif (NOT "$ENV{CUDA_TOOLKIT_ROOT_DIR}" STREQUAL "")
                set (CUDAToolkit_ROOT "$ENV{CUDA_TOOLKIT_ROOT_DIR}")
            endif ()
        endif ()

        if (NOT CUDAToolkit_FIND_QUIETLY OR NOT OptiX_FIND_QUIETLY)
            message (STATUS "CUDAToolkit_ROOT = ${CUDAToolkit_ROOT}")
        endif ()

        checked_find_package (CUDAToolkit REQUIRED
                              VERSION_MIN 9.0
                              RECOMMEND_MIN 11.0
                              RECOMMEND_MIN_REASON
                                  "We don't actively test CUDA older than 11"
                              PRINT CUDAToolkit_INCLUDE_DIRS
                                    CUDAToolkit_NVCC_EXECUTABLE)

        # CUDA 13 moved its C++ Core Libraries (CCCL) under include/cccl.
        # New CMake releases report that path, but older supported releases
        # do not, so augment the discovered list when that directory exists.
        set (OSL_CUDA_INCLUDE_DIRS ${CUDAToolkit_INCLUDE_DIRS})
        foreach (cuda_include_dir IN LISTS CUDAToolkit_INCLUDE_DIRS)
            if (IS_DIRECTORY "${cuda_include_dir}/cccl")
                list (APPEND OSL_CUDA_INCLUDE_DIRS
                      "${cuda_include_dir}/cccl")
            endif ()
        endforeach ()
        list (FILTER OSL_CUDA_INCLUDE_DIRS EXCLUDE REGEX "^$")
        list (REMOVE_DUPLICATES OSL_CUDA_INCLUDE_DIRS)
        include_directories (BEFORE ${OSL_CUDA_INCLUDE_DIRS})

        # Clang's --cuda-path needs the toolkit root, not the target-specific
        # library directory. FindCUDAToolkit normally reports it directly.
        if (CUDAToolkit_LIBRARY_ROOT)
            set (OSL_CUDA_TOOLKIT_ROOT "${CUDAToolkit_LIBRARY_ROOT}")
        elseif (CUDAToolkit_BIN_DIR)
            get_filename_component (OSL_CUDA_TOOLKIT_ROOT
                                    "${CUDAToolkit_BIN_DIR}" DIRECTORY)
        else ()
            message (FATAL_ERROR
                     "Could not determine the CUDA toolkit root directory")
        endif ()

        STRING (FIND ${LLVM_TARGETS} "NVPTX" nvptx_index)
        if (NOT ${nvptx_index} GREATER -1)
            message (FATAL_ERROR "NVPTX target is not available in the provided LLVM build")
        endif()

        set (OSL_CUDA_LIB_FLAGS "--cuda-path=${OSL_CUDA_TOOLKIT_ROOT}")

        # Imported targets carry the platform-specific system dependencies
        # needed by the selected CUDA runtime, including on Windows.
        if (CUDA_PREFER_STATIC_LIBS)
            set (OSL_CUDA_RUNTIME_TARGET CUDA::cudart_static)
        else ()
            set (OSL_CUDA_RUNTIME_TARGET CUDA::cudart)
        endif ()
        if (NOT TARGET ${OSL_CUDA_RUNTIME_TARGET})
            message (FATAL_ERROR
                     "CUDA runtime target ${OSL_CUDA_RUNTIME_TARGET} not found")
        endif ()
    endif()

    # OptiX setup
    if (OSL_USE_OPTIX AND OSL_BUILD_TESTS)
        checked_find_package (OptiX REQUIRED
                              VERSION_MIN 7.0)
        include_directories (BEFORE "${OPTIX_INCLUDES}")
        if (NOT USE_LLVM_BITCODE OR NOT USE_FAST_MATH)
            message (FATAL_ERROR "Enabling OptiX requires USE_LLVM_BITCODE=1 and USE_FAST_MATH=1")
        endif ()
    endif ()

    function (osl_optix_target TARGET)
        target_include_directories (${TARGET} BEFORE PRIVATE
                                    ${OSL_CUDA_INCLUDE_DIRS}
                                    ${OPTIX_INCLUDES})
        ## XXX: Should -DPTX_PATH point to (or include) CMAKE_CURRENT_BINARY_DIR so tests can run before installation ?
        target_compile_definitions (${TARGET} PRIVATE PTX_PATH="${OSL_PTX_INSTALL_DIR}")
        target_link_libraries (${TARGET} PRIVATE
                               ${OSL_CUDA_RUNTIME_TARGET}
                               ${CUDA_EXTRA_LIBS}
                               ${OPTIX_LIBRARIES}
                               ${OPTIX_EXTRA_LIBS})
    endfunction()
else ()
    message(STATUS "CUDA/OptiX support disabled")
    function (osl_optix_target TARGET)
    endfunction()
endif ()


# AMDGPU setup: the device shadeops are compiled with OSL's own clang in HIP
# mode, and the ROCm device libraries (ocml, ockl, oclc_*) are linked in by
# the clang driver. No HIP runtime is needed to build them.
if (OSL_ENABLE_AMDGPU)
    if (NOT USE_LLVM_BITCODE)
        message (FATAL_ERROR "OSL_ENABLE_AMDGPU requires USE_LLVM_BITCODE=ON")
    endif ()
    if (NOT LLVM_TARGETS MATCHES "AMDGPU")
        message (FATAL_ERROR "The AMDGPU target is not available in the provided LLVM build")
    endif ()
    # Code object version 6, which the device shadeops are built for, needs
    # LLVM 19 or newer.
    if (LLVM_VERSION VERSION_LESS 19.0)
        message (FATAL_ERROR "OSL_ENABLE_AMDGPU requires LLVM 19 or newer (you have LLVM ${LLVM_VERSION})")
    endif ()
    foreach (arch ${OSL_HIP_TARGET_ARCHS})
        if (NOT arch MATCHES "^gfx[0-9a-f]+$")
            message (FATAL_ERROR "Invalid AMDGPU architecture '${arch}' in OSL_HIP_TARGET_ARCHS")
        endif ()
    endforeach ()

    if (NOT EXISTS "${ROCM_PATH}/include/hip")
        message (FATAL_ERROR "ROCM_PATH (\"${ROCM_PATH}\") does not contain the HIP headers")
    endif ()
    find_path (OSL_ROCM_DEVICE_LIB_PATH NAMES ocml.bc
               HINTS "${ROCM_PATH}/lib/llvm/amdgcn/bitcode"
                     "${ROCM_PATH}/llvm/amdgcn/bitcode"
                     "${ROCM_PATH}/amdgcn/bitcode"
               NO_DEFAULT_PATH)
    if (NOT OSL_ROCM_DEVICE_LIB_PATH)
        message (FATAL_ERROR "Cannot find the ROCm device libraries (ocml.bc) under ROCM_PATH (\"${ROCM_PATH}\")")
    endif ()
    set (required_device_libs ocml ockl oclc_abi_version_600)
    foreach (arch ${OSL_HIP_TARGET_ARCHS})
        string (REPLACE "gfx" "" isa "${arch}")
        list (APPEND required_device_libs oclc_isa_version_${isa})
    endforeach ()
    foreach (lib ${required_device_libs})
        if (NOT EXISTS "${OSL_ROCM_DEVICE_LIB_PATH}/${lib}.bc")
            message (FATAL_ERROR "Missing ROCm device library ${OSL_ROCM_DEVICE_LIB_PATH}/${lib}.bc")
        endif ()
    endforeach ()

    # Bitcode is not readable by an older LLVM, so the tools that produce the
    # embedded shadeops must be exactly OSL's LLVM version. The testsuite
    # uses llc to compile the emitted artifacts, so the same applies to it.
    foreach (var_tool OSL_AMDGPU_CLANG:clang++ OSL_AMDGPU_LLVM_LINK:llvm-link
                      OSL_AMDGPU_OPT:opt OSL_AMDGPU_LLVM_DIS:llvm-dis
                      OSL_AMDGPU_LLC:llc)
        string (REPLACE ":" ";" var_tool "${var_tool}")
        list (GET var_tool 0 var)
        list (GET var_tool 1 tool)
        find_program (${var} NAMES ${tool} PATHS "${LLVM_DIRECTORY}/bin"
                      NO_DEFAULT_PATH)
        if (NOT ${var})
            message (FATAL_ERROR "Cannot find ${tool} in ${LLVM_DIRECTORY}/bin")
        endif ()
        execute_process (COMMAND "${${var}}" --version
                         OUTPUT_VARIABLE tool_version ERROR_QUIET)
        if (NOT tool_version MATCHES "version ([0-9]+\\.[0-9]+\\.[0-9]+)"
            OR NOT CMAKE_MATCH_1 VERSION_EQUAL LLVM_VERSION)
            message (FATAL_ERROR "${${var}} must be LLVM ${LLVM_VERSION}")
        endif ()
    endforeach ()

    # The device libraries are bitcode built by ROCm's own LLVM. If that is
    # newer than OSL's, OSL's tools cannot read them, and the shadeops build
    # would fail much later with a less clear error.
    set (dis_out "${CMAKE_BINARY_DIR}/CMakeFiles/osl_rocm_devlib_check.ll")
    foreach (lib ${required_device_libs})
        execute_process (COMMAND "${OSL_AMDGPU_LLVM_DIS}"
                                 "${OSL_ROCM_DEVICE_LIB_PATH}/${lib}.bc"
                                 -o "${dis_out}"
                         RESULT_VARIABLE dis_result
                         ERROR_VARIABLE dis_error)
        file (REMOVE "${dis_out}")
        if (NOT dis_result EQUAL 0)
            message (FATAL_ERROR "LLVM ${LLVM_VERSION} cannot read the ROCm device library ${OSL_ROCM_DEVICE_LIB_PATH}/${lib}.bc. ROCm was probably built with a newer LLVM; use a ROCm whose LLVM is not newer than OSL's. The error was: ${dis_error}")
        endif ()
    endforeach ()
    message (STATUS "AMDGPU device shadeops for: ${OSL_HIP_TARGET_ARCHS}")
    message (STATUS "ROCm device libraries: ${OSL_ROCM_DEVICE_LIB_PATH}")
else ()
    message (STATUS "AMDGPU device shadeops disabled")
endif ()

# HART: only one header of the HART SDK is used, for the value of the
# device-storage marker that HART requires in every shader module. Nothing is
# linked. That header includes the HIP runtime API header from ROCm.
if (OSL_USE_HART)
    if (NOT OSL_ENABLE_AMDGPU)
        message (FATAL_ERROR "OSL_USE_HART requires OSL_ENABLE_AMDGPU=ON")
    endif ()
    find_path (HART_INCLUDE_DIR NAMES amd/hart/hart_device.h
               HINTS ${HART_ROOT} $ENV{HART_ROOT}
               PATH_SUFFIXES include)
    if (NOT HART_INCLUDE_DIR)
        message (FATAL_ERROR "OSL_USE_HART: cannot find amd/hart/hart_device.h; set HART_ROOT to the HART SDK")
    endif ()
    message (STATUS "HART headers: ${HART_INCLUDE_DIR}")
endif ()


###########################################################################
# Tessil/robin-map

option (BUILD_ROBINMAP_FORCE "Force local download/build of robin-map even if installed" OFF)
option (BUILD_MISSING_ROBINMAP "Local download/build of robin-map if not installed" ON)
set (BUILD_ROBINMAP_VERSION "v0.6.2" CACHE STRING "Preferred Tessil/robin-map version, of downloading/building our own")

macro (find_or_download_robin_map)
    # If we weren't told to force our own download/build of robin-map, look
    # for an installed version. Still prefer a copy that seems to be
    # locally installed in this tree.
    if (NOT BUILD_ROBINMAP_FORCE)
        find_package (Robinmap QUIET)
    endif ()
    # If an external copy wasn't found and we requested that missing
    # packages be built, or we we are forcing a local copy to be built, then
    # download and build it.
    # Download the headers from github
    if ((BUILD_MISSING_ROBINMAP AND NOT ROBINMAP_FOUND) OR BUILD_ROBINMAP_FORCE)
        message (STATUS "Downloading local Tessil/robin-map")
        set (ROBINMAP_INSTALL_DIR "${PROJECT_SOURCE_DIR}/ext/robin-map")
        set (ROBINMAP_GIT_REPOSITORY "https://github.com/Tessil/robin-map")
        if (NOT IS_DIRECTORY ${ROBINMAP_INSTALL_DIR}/include/tsl)
            find_package (Git REQUIRED)
            execute_process(COMMAND             ${GIT_EXECUTABLE} clone    ${ROBINMAP_GIT_REPOSITORY} -n ${ROBINMAP_INSTALL_DIR})
            execute_process(COMMAND             ${GIT_EXECUTABLE} checkout ${BUILD_ROBINMAP_VERSION}
                            WORKING_DIRECTORY   ${ROBINMAP_INSTALL_DIR})
            if (IS_DIRECTORY ${ROBINMAP_INSTALL_DIR}/include/tsl)
                message (STATUS "DOWNLOADED Tessil/robin-map to ${ROBINMAP_INSTALL_DIR}.\n"
                         "Remove that dir to get rid of it.")
            else ()
                message (FATAL_ERROR "Could not download Tessil/robin-map")
            endif ()
        endif ()
        set (ROBINMAP_INCLUDE_DIR "${ROBINMAP_INSTALL_DIR}/include")
    endif ()
    checked_find_package (Robinmap REQUIRED)
endmacro()

find_or_download_robin_map ()
