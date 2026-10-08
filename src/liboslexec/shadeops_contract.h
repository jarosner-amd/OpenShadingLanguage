// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <OSL/platform.h>

// OSL_SHADEOPS_RS_ONLY is true when the shadeops reach the renderer only
// through the rs_* functions, so OSL builds the rest itself: on the CPU, and
// for HIP on AMD GPUs (__HIP__). It is false for CUDA, where the renderer also
// supplies some of the osl_* functions itself (see testrender's rend_lib.cu);
// each guarded site names the ones it covers. This is a renderer contract, not
// a compiler fact, so test this rather than a vendor macro. It describes how
// liboslexec and its shadeops are built, so it is private and not a setting.
#if OSL_GPU && !defined(__HIP__)
#    define OSL_SHADEOPS_RS_ONLY 0
#else
#    define OSL_SHADEOPS_RS_ONLY 1
#endif
