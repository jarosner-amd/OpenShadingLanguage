// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <OSL/oslexec.h>

OSL_NAMESPACE_BEGIN

// Print the AMDGPU artifacts of a compiled shader group (architecture,
// triple, format and exports of each), and with `save` also write each one
// to amdgpu_<stem>_<arch>.bc or .ll in the current directory. Characters of
// `stem` that do not belong in a file name become '_'. Sets `num_artifacts`.
// Returns false if an artifact could not be queried, read or written.
// Shared by testshade and testrender.
bool
report_amdgpu_artifacts(ShadingSystem& ss, ShaderGroup& group, string_view stem,
                        bool save, int& num_artifacts);

OSL_NAMESPACE_END
