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

# The renderer services of testshade and testrender
# (src/test_common/rs_simplerend.cpp) as AMDGPU bitcode, one file per
# architecture in OSL_HIP_TARGET_ARCHS, embedded in the calling program with a
# lookup table, amdgpu_rend_lib(arch) (src/testshade/amdgpu_rend_lib.cpp.in).
# The program links them with each shader group at run time (hiprtc). The
# embedded symbols are named ${prefix}_amdgpu_rend_lib_<arch>, and the custom
# target that builds the bitcode is ${prefix}_amdgpu_rend_lib. Appends the
# sources to compile (the embedded blocks, the table, the HIP loader and the
# GPU textures) to the list named by srcs_var. The program also needs
# src/testshade and ${ROCM_PATH}/include on its include path.
function ( EMBED_AMDGPU_REND_LIB prefix srcs_var )
    set (testshade_dir "${CMAKE_SOURCE_DIR}/src/testshade")
    set (include_dirs ${OpenImageIO_INCLUDES}
         ${CMAKE_SOURCE_DIR}/src/include ${CMAKE_BINARY_DIR}/include
         ${IMATH_INCLUDES})
    set (srcs ${${srcs_var}})
    set (amdgpu_rend_lib_decls "")
    set (amdgpu_rend_lib_entries "")
    foreach (arch ${OSL_HIP_TARGET_ARCHS})
        MAKE_HIP_BITCODE ( ${CMAKE_SOURCE_DIR}/src/test_common/rs_simplerend.cpp
                           "_amdgpu_${arch}" ${arch} rend_lib_amdgpu_bc
                           "${include_dirs}" )
        list (APPEND rend_lib_amdgpu_bcs ${rend_lib_amdgpu_bc})
        set (sym "${prefix}_amdgpu_rend_lib_${arch}")
        set (rend_lib_amdgpu_cpp "${CMAKE_CURRENT_BINARY_DIR}/rend_lib_amdgpu_${arch}.bc.cpp")
        MAKE_EMBEDDED_CPP ( ${sym} ${rend_lib_amdgpu_cpp} ${rend_lib_amdgpu_bc} )
        list (APPEND srcs ${rend_lib_amdgpu_cpp})
        string (APPEND amdgpu_rend_lib_decls
                "extern unsigned char ${sym}_block[];\nextern int ${sym}_size;\n")
        string (APPEND amdgpu_rend_lib_entries
                "        { \"${arch}\", ${sym}_block, &${sym}_size },\n")
    endforeach ()
    add_custom_target (${prefix}_amdgpu_rend_lib ALL
        DEPENDS ${rend_lib_amdgpu_bcs})
    configure_file (${testshade_dir}/amdgpu_rend_lib.cpp.in
                    "${CMAKE_CURRENT_BINARY_DIR}/amdgpu_rend_lib.cpp" @ONLY)
    # The HIP runtime and hiprtc are loaded at run time (hiploader.cpp), so
    # only their headers are needed, not their libraries.
    list (APPEND srcs ${testshade_dir}/hiploader.cpp
          ${testshade_dir}/amdgpu_textures.cpp
          "${CMAKE_CURRENT_BINARY_DIR}/amdgpu_rend_lib.cpp")
    set (${srcs_var} ${srcs} PARENT_SCOPE)
endfunction ()
