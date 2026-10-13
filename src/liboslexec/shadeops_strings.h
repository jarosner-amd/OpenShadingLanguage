// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <OSL/hashes.h>

// Strings that the shadeops themselves pass to the renderer's errorfmt and
// friends, as a format or as an argument. On a GPU device only their hashes
// exist, so the host must hold each one as a ustring for a renderer to
// decode the message (ustring::from_hash). shadingsys.cpp makes a ustring of
// every entry when liboslexec is loaded; shadeops use the hashes below, so
// the text cannot differ between the two.
#define OSL_SHADEOPS_STRINGS(X)                                           \
    X("Index [{}] out of range {}[0..{}]: {}:{} (group {}, layer {} {}, " \
      "shader {})",                                                       \
      range_check_error)                                                  \
    X("<unnamed group>", unnamed_group)                                   \
    X("<unnamed layer>", unnamed_layer)                                   \
    X("Unknown transformation \"{}\"", unknown_transformation)            \
    X("Color space transformation \"{}\" -> \"{}\" is not available on "  \
      "this device",                                                      \
      color_transform_unavailable)                                        \
    X("Unknown noise type \"{}\"", unknown_noise_type)                    \
    X("message \"{}\" already exists (created here: {}:{})"               \
      " cannot set again from {}:{}",                                     \
      message_exists)                                                     \
    X("message \"{}\" was queried before being set (queried here: {}:{})" \
      " setting it now ({}:{}) would lead to inconsistent results",       \
      message_queried_before_set)                                         \
    X("type mismatch for message \"{}\" ({} as {} here: {}:{})"           \
      " cannot fetch as {} from {}:{}",                                   \
      message_type_mismatch)                                              \
    X("message \"{}\" was set by layer #{} ({}:{})"                       \
      " but is being queried by layer #{} ({}:{})"                        \
      " - messages may only be transferred from nodes "                   \
      "that appear earlier in the shading network",                       \
      message_wrong_layer_order)                                          \
    X("created", created)                                                 \
    X("queried", queried)

OSL_NAMESPACE_BEGIN
namespace pvt {
namespace ShadeopsHashes {
#ifdef __CUDA_ARCH__  // as in OSL/hashes.h
#    define OSL_SHADEOPS_HASH(str, var_name) \
        __device__ const OSL::ustringhash var_name(OSL::strhash(str));
#else
#    define OSL_SHADEOPS_HASH(str, var_name) \
        constexpr OSL::ustringhash var_name(OSL::strhash(str));
#endif
OSL_SHADEOPS_STRINGS(OSL_SHADEOPS_HASH)
#undef OSL_SHADEOPS_HASH
}  // namespace ShadeopsHashes
}  // namespace pvt
OSL_NAMESPACE_END
