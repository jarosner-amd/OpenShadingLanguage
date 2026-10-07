# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Code object version of the AMDGPU device shadeops. It must match the
# default code_obj_version in GPUTargetDesc (OSL/gpu_target_desc.h): the
# shader group and the shadeops it links must agree on it.
set (OSL_AMDGPU_CODE_OBJECT_VERSION 6)

# Compile one C++ source file to AMDGPU LLVM bitcode for one GPU architecture.
#
# This uses OSL's own clang in HIP mode, compiling only for the device.
# Given --rocm-device-lib-path, the clang driver links the ROCm device
# libraries (ocml, ockl and the per-architecture oclc_* control libraries)
# into the bitcode, so device math resolves inside OSL's build.
function ( MAKE_HIP_BITCODE src suffix arch generated_bc include_dirs )
    get_filename_component ( src_we ${src} NAME_WE )
    set (bc "${CMAKE_CURRENT_BINARY_DIR}/${src_we}${suffix}.bc")
    set (${generated_bc} ${bc} PARENT_SCOPE)

    get_property (CURRENT_DEFINITIONS DIRECTORY PROPERTY COMPILE_DEFINITIONS)
    foreach (def ${CURRENT_DEFINITIONS})
        list (APPEND hip_flags "-D${def}")
    endforeach ()
    list (TRANSFORM include_dirs PREPEND -I OUTPUT_VARIABLE hip_includes)

    if (USE_FAST_MATH)
        # Not -ffast-math: with it, clang links the finite-only device
        # libraries, and isnan/isinf checks may be folded away. Also not
        # -freciprocal-math or -fapprox-func: with either, a / b becomes
        # a * (1/b) with a fast reciprocal, and 1/b overflows to Inf for a
        # tiny b although a / b is finite, so safe_div returns 0 where the
        # CPU returns the quotient.
        set (hip_math_flags -fno-math-errno -fassociative-math
             -fno-signed-zeros -fno-trapping-math -fno-rounding-math
             -ffp-contract=fast)
    else ()
        set (hip_math_flags -fno-math-errno)
    endif ()

    add_custom_command (OUTPUT ${bc}
        COMMAND ${OSL_AMDGPU_CLANG}
            -x hip --offload-device-only --no-gpu-bundle-output
            --offload-arch=${arch}
            # Each source is compiled separately and the results are linked
            # by llvm-link, which is relocatable device code by definition.
            -fgpu-rdc
            "--rocm-path=${ROCM_PATH}"
            "--rocm-device-lib-path=${OSL_ROCM_DEVICE_LIB_PATH}"
            -mcode-object-version=${OSL_AMDGPU_CODE_OBJECT_VERSION}
            ${hip_includes} ${hip_flags} ${CSTD_FLAGS}
            -DOSL_COMPILING_TO_BITCODE=1 -DNDEBUG -DOIIO_NO_SSE
            -Wno-deprecated-register -Wno-ignored-attributes
            -Wno-unknown-attributes
            ${hip_math_flags} -O3 -emit-llvm -c ${src} -o ${bc}
        DEPENDS ${exec_headers} ${PROJECT_PUBLIC_HEADERS} ${src}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM )
endfunction ()

# Compile the device shadeops for one AMDGPU architecture: each source to
# bitcode, then llvm-link them and optimize the result. Also writes an .ll
# copy next to the .bc, for tests that check its contents.
function ( HIP_SHADEOPS_COMPILE prefix arch output_bc input_srcs include_dirs )
    set (linked_bc "${CMAKE_CURRENT_BINARY_DIR}/${prefix}_${arch}.bc")
    set (linked_ll "${CMAKE_CURRENT_BINARY_DIR}/${prefix}_${arch}.ll")
    set (unoptimized_bc "${CMAKE_CURRENT_BINARY_DIR}/${prefix}_${arch}_unoptimized.bc")
    set (${output_bc} ${linked_bc} PARENT_SCOPE)

    foreach (shadeops_src ${input_srcs})
        MAKE_HIP_BITCODE (${shadeops_src} "_${prefix}_${arch}" ${arch}
                          shadeops_bc "${include_dirs}")
        list (APPEND shadeops_bc_list ${shadeops_bc})
    endforeach ()

    add_custom_command (OUTPUT ${linked_bc} ${linked_ll}
        BYPRODUCTS ${unoptimized_bc}
        COMMAND ${OSL_AMDGPU_LLVM_LINK} ${shadeops_bc_list} -o ${unoptimized_bc}
        COMMAND ${OSL_AMDGPU_OPT} "-passes=default<O3>,verify"
            ${unoptimized_bc} -o ${linked_bc}
        COMMAND ${OSL_AMDGPU_LLVM_DIS} ${linked_bc} -o ${linked_ll}
        DEPENDS ${shadeops_bc_list}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM )
endfunction ()
