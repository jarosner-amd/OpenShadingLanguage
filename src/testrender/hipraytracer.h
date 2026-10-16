// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include "simpleraytracer.h"

OSL_NAMESPACE_BEGIN

// The AMDGPU side of testrender, for now without a GPU runtime: it compiles
// every shader group of the scene for AMDGPU (the ShadingSystem must have
// gpu_backend "amdgpu") and reports, and optionally saves, the artifacts.
// Nothing is launched and no image is made.
class HipRaytracer final : public SimpleRaytracer {
public:
    HipRaytracer() = default;

    // Write each artifact to amdgpu_<material>_<group>_<arch>.bc or .ll.
    void save_artifacts(bool save) { m_save = save; }

    // Compile the scene's shader groups; does not run any shader.
    void prepare_render() override;
    // No GPU launch yet: these do nothing.
    void warmup() override {}
    void render(int xres, int yres) override {}

    // False if a group failed to compile or produced no artifact.
    bool ok() const { return m_ok; }

private:
    bool compile_group(ShaderGroup& group, string_view stem);

    bool m_save = false;
    bool m_ok   = true;
};

OSL_NAMESPACE_END
