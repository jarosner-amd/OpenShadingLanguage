// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <OSL/oslconfig.h>

OSL_NAMESPACE_BEGIN

namespace pvt {

/// The AMDGPU device shadeops bitcode embedded for one GPU architecture,
/// such as "gfx1100". Returns an empty span if this build has none for it.
cspan<unsigned char>
find_amdgpu_shadeops(string_view arch);

/// The architectures that have embedded AMDGPU device shadeops, as a comma
/// separated list. Empty if this build has none.
string_view
embedded_amdgpu_archs();

}  // namespace pvt

OSL_NAMESPACE_END
